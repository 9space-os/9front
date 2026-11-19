/*
 * SPDX-License-Identifier: MIT
 *
 * glage: buffer age and partial redraw, as WebRender (Servo) draws:
 * each frame asks the back buffer's age (EGL_EXT_buffer_age) and redraws
 * only what changed since the frame that buffer holds, then swaps with
 * the damage (EGL_KHR_swap_buffers_with_damage where there is one).  The
 * picture: a dark blue background and one stripe that moves down a few
 * rows each frame (its green the frame's number).  With age a the
 * stripes of the frames since (f-a .. f-1) are cleared back to the
 * background, the new one drawn; age 0 draws all.  Before each swap the
 * column in the middle is read back whole (glReadPixels of the back
 * buffer): background everywhere but the stripe of this frame.  An age
 * that says newer than the buffer's content leaves an older stripe
 * standing (old content flickering in while Servo scrolls: the X1,
 * 2026-11-19).
 * GLAGE_DRAW=1: the fills drawn (a triangle cut by the scissor), not glClear;
 * GLAGE_FINISH=1: glFinish before each read back.
 * -s (in a window of wsys -g): every 30 frames the window as wsys composed
 * it (/dev/window, image(6): one read is one frame), its middle column: one
 * stripe at most (a client's damage composed wrongly by wsys -- its
 * VK_KHR_incremental_present -- leaves old ones there).
 *	glage [-s] [frames]
 * Lines "ages ...", "screen ...", then "glage: all passed" or "glage: FAILED".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <unistd.h>

enum { Stripe = 8, Step = 5, Maxage = 8 };

static double
now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

/* the stripe's first row (from the top) in frame f of a window h rows high */
static int
stripey(int f, int h)
{
	return (f * Step) % (h - Stripe);
}

/* the stripe's green in frame f */
static int
green(int f)
{
	return 40 + f % 200;
}

/* GLAGE_DRAW: the fills drawn (a triangle over all, cut by the scissor), not cleared */
static GLuint prog;
static GLint colour;

static GLuint
shader(GLenum type, const char *src)
{
	GLuint s;

	s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	return s;
}

static void
mkprog(void)
{
	static const GLfloat tri[] = { -1, -1, 3, -1, -1, 3 };

	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER,
		"attribute vec2 pos;\nvoid main() { gl_Position = vec4(pos, 0.0, 1.0); }\n"));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER,
		"precision mediump float;\nuniform vec4 c;\nvoid main() { gl_FragColor = c; }\n"));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glUseProgram(prog);
	colour = glGetUniformLocation(prog, "c");
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
	glEnableVertexAttribArray(0);
}

/* rows y0 .. y0+n-1 (from the top) to the stripe's colour or the background */
static void
fill(int w, int h, int y0, int n, int g, int stripe)
{
	GLfloat c[4];

	c[0] = stripe ? 1.0f : 0.0f;
	c[1] = stripe ? g / 255.0f : 0.0f;
	c[2] = stripe ? 0.0f : 0.4f;
	c[3] = 1.0f;
	glScissor(0, h - y0 - n, w, n);
	if(prog){
		glUniform4fv(colour, 1, c);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		return;
	}
	glClearColor(c[0], c[1], c[2], c[3]);
	glClear(GL_COLOR_BUFFER_BIT);
}

/*
 * the window as composed (/dev/window: image(6), x8r8g8b8 rows): rows of
 * the stripe's colours in its middle column; -1 if it cannot be read
 */
static int
composedstripes(void)
{
	static unsigned char *buf;
	static size_t cap;
	size_t n;
	ssize_t r;
	int fd, x0, y0, x1, y1, x, y, rows;
	char chan[16];
	unsigned char *p;

	if((fd = open("/dev/window", O_RDONLY)) < 0)
		return -1;
	n = 0;
	for(;;){
		if(n == cap){
			cap = cap ? 2*cap : 1<<20;
			buf = realloc(buf, cap);
		}
		r = read(fd, buf + n, cap - n);
		if(r <= 0)
			break;
		n += r;
	}
	close(fd);
	if(n < 60 || sscanf((char*)buf, "%11s %d %d %d %d", chan, &x0, &y0, &x1, &y1) != 5 ||
	   strcmp(chan, "x8r8g8b8") != 0 || n < 60 + (size_t)(x1 - x0) * (y1 - y0) * 4)
		return -1;
	x = (x1 - x0) / 2;
	rows = 0;
	for(y = 0; y < y1 - y0; y++){
		p = buf + 60 + ((size_t)y * (x1 - x0) + x) * 4;	/* b g r x */
		if(p[2] >= 0xf0 && p[0] <= 0x10 && p[1] >= 30 && p[1] <= 245)
			rows++;
	}
	return rows;
}

int
main(int argc, char **argv)
{
	static const EGLint cattr[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_NONE,
	};
	static const EGLint xattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC swapdamage;
	EGLDisplay d;
	EGLConfig c;
	EGLSurface s;
	EGLContext x;
	EGLint maj, min, n, w, h, age, rect[4*(Maxage+1)];
	unsigned char *col;
	int frames, f, i, y, nr, bad, stale, lastw, lasth, ages[Maxage+2];
	double t0, t;
	const char *ext;
	int screen, nsamp, nmore, nnone, k;

	screen = argc > 1 && strcmp(argv[1], "-s") == 0;
	if(screen){
		argc--;
		argv++;
	}
	nsamp = nmore = nnone = 0;
	frames = argc > 1 ? atoi(argv[1]) : 600;
	d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if(d == EGL_NO_DISPLAY || !eglInitialize(d, &maj, &min)){
		printf("FAIL eglInitialize: %#x\n", eglGetError());
		return 1;
	}
	ext = eglQueryString(d, EGL_EXTENSIONS);
	if(ext == NULL || strstr(ext, "EGL_EXT_buffer_age") == NULL){
		printf("FAIL no EGL_EXT_buffer_age\n");
		return 1;
	}
	swapdamage = NULL;
	if(strstr(ext, "EGL_KHR_swap_buffers_with_damage") != NULL)
		swapdamage = (PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC)eglGetProcAddress("eglSwapBuffersWithDamageKHR");
	if(!eglChooseConfig(d, cattr, &c, 1, &n) || n < 1){
		printf("FAIL no window config: %#x\n", eglGetError());
		return 1;
	}
	s = eglCreateWindowSurface(d, c, (EGLNativeWindowType)"", NULL);	/* "": this program's window */
	if(s == EGL_NO_SURFACE){
		printf("FAIL eglCreateWindowSurface: %#x\n", eglGetError());
		return 1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	x = eglCreateContext(d, c, EGL_NO_CONTEXT, xattr);
	if(x == EGL_NO_CONTEXT || !eglMakeCurrent(d, s, s, x)){
		printf("FAIL context: %#x\n", eglGetError());
		return 1;
	}
	eglQuerySurface(d, s, EGL_WIDTH, &w);
	eglQuerySurface(d, s, EGL_HEIGHT, &h);
	printf("glage: EGL %d.%d, window %dx%d, %s\n", maj, min, w, h,
		swapdamage != NULL ? "swap with damage" : "plain swap");
	glEnable(GL_SCISSOR_TEST);
	if(getenv("GLAGE_DRAW") != NULL)
		mkprog();
	col = NULL;
	lastw = lasth = -1;
	memset(ages, 0, sizeof ages);
	bad = stale = 0;
	t0 = now();
	for(f = 0; f < frames; f++){
		eglQuerySurface(d, s, EGL_WIDTH, &w);
		eglQuerySurface(d, s, EGL_HEIGHT, &h);
		if(h <= 2*Stripe){
			printf("FAIL window %dx%d too small\n", w, h);
			return 1;
		}
		if(!eglQuerySurface(d, s, EGL_BUFFER_AGE_EXT, &age))
			age = 0;
		ages[age < 0 ? Maxage+1 : age > Maxage ? Maxage+1 : age]++;
		if(w != lastw || h != lasth){
			age = 0;	/* a new size: all of it */
			free(col);
			col = malloc(4 * h);
			lastw = w;
			lasth = h;
		}
		glViewport(0, 0, w, h);
		nr = 0;
		if(age <= 0 || age > f || age > Maxage){
			fill(w, h, 0, h, 0, 0);
			rect[0] = 0;
			rect[1] = 0;
			rect[2] = w;
			rect[3] = h;
			nr = 1;
		}else{
			/* the stripes of frames f-age .. f-1 back to the background */
			for(i = f - age; i < f; i++){
				y = stripey(i, h);
				fill(w, h, y, Stripe, 0, 0);
				rect[4*nr] = 0;
				rect[4*nr+1] = h - y - Stripe;
				rect[4*nr+2] = w;
				rect[4*nr+3] = Stripe;
				nr++;
			}
		}
		y = stripey(f, h);
		fill(w, h, y, Stripe, green(f), 1);
		rect[4*nr] = 0;
		rect[4*nr+1] = h - y - Stripe;
		rect[4*nr+2] = w;
		rect[4*nr+3] = Stripe;
		nr++;

		/* the middle column: background but this frame's stripe */
		if(getenv("GLAGE_FINISH") != NULL)
			glFinish();
		glReadPixels(w/2, 0, 1, h, GL_RGBA, GL_UNSIGNED_BYTE, col);
		for(i = 0; i < h; i++){
			unsigned char *p = col + 4*(h - 1 - i);	/* row i from the top */
			int in = i >= y && i < y + Stripe;

			if(in ? (p[0] < 0xf0 || abs(p[1] - green(f)) > 2) : (p[0] > 0x10 || p[1] > 0x10 || abs(p[2] - 0x66) > 2)){
				if(bad < 5)
					printf("glage: frame %d (age %d): row %d is %02x%02x%02x, not %s\n",
						f, age, i, p[0], p[1], p[2], in ? "the stripe" : "the background");
				bad++;
				if(!in && p[0] >= 0xf0)
					stale++;	/* an older frame's stripe */
				break;
			}
		}
		if(swapdamage != NULL ? !swapdamage(d, s, rect, nr) : !eglSwapBuffers(d, s)){
			printf("FAIL swap frame %d: %#x\n", f, eglGetError());
			return 1;
		}
		if(screen && f >= 30 && f % 30 == 0){
			k = composedstripes();
			if(k < 0)
				nnone++;
			else{
				nsamp++;
				if(k > Stripe){
					if(nmore < 5)
						printf("glage: frame %d: the window as composed has %d rows of stripes (one is %d)\n", f, k, Stripe);
					nmore++;
				}
			}
		}
	}
	t = now() - t0;
	printf("ages");
	for(i = 0; i <= Maxage; i++)
		if(ages[i])
			printf(" %d:%d", i, ages[i]);
	if(ages[Maxage+1])
		printf(" other:%d", ages[Maxage+1]);
	printf("\n");
	printf("glage: %d frames %dx%d in %.2f s: %.1f fps, %d frames wrong (%d with an older stripe)\n",
		frames, w, h, t, frames / t, bad, stale);
	if(screen)
		printf("screen: %d samples of the window as composed, %d with old stripes, %d unread\n", nsamp, nmore, nnone);
	if(screen && (nsamp == 0 || nmore > 0))
		bad++;
	printf("%s\n", bad == 0 && glGetError() == GL_NO_ERROR ? "glage: all passed" : "glage: FAILED");
	eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglTerminate(d);
	return bad != 0;
}
