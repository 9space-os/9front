/*
 * GLFW 3.5 for 9front - www.glfw.org
 * SPDX-License-Identifier: Zlib
 *
 * plan9_draw.c, the libdraw side of the plan9 platform: plain C types,
 * so that both sides (APE's libdraw, GLFW's headers) include it.
 */
typedef struct _GLFWdrawwinPlan9 _GLFWdrawwinPlan9;	/* a window: the process's, or one of wsys' clone file */

int	_glfwDrawInitPlan9(void);	/* the display and the process's window: 0, -1 */
void	_glfwDrawClosePlan9(void);
_GLFWdrawwinPlan9*	_glfwDrawProcessPlan9(void);	/* the process's window (/dev) */
/* another window of its own (wsys only, its clone file): msgs wsys ctl lines; nil, the error in errstr */
_GLFWdrawwinPlan9*	_glfwDrawOpenPlan9(const char *msgs);
void	_glfwDrawCloseWinPlan9(_GLFWdrawwinPlan9*);
const char*	_glfwDrawDirPlan9(_GLFWdrawwinPlan9*);	/* its files: /dev, or a directory that stays across wsys restarts */
int	_glfwDrawIsProcessPlan9(_GLFWdrawwinPlan9*);
int	_glfwDrawCtlPlan9(_GLFWdrawwinPlan9*, const char *msg);	/* a wsys ctl message (not the process's window) */
int	_glfwDrawAttachPlan9(_GLFWdrawwinPlan9*);	/* the window again (resized: rio gives it a new name) */
int	_glfwDrawAgainPlan9(void);	/* the process's window's wsys gone: it (and the others) in the next one: 0, -1 */
int	_glfwDrawReconnectPlan9(void);	/* draw(3) anew after _glfwDrawAgainPlan9 */
void	_glfwDrawWsysPlan9(char *buf, int n);	/* $wsys: the wsys the windows are in now */
int	_glfwDrawWsysLostPlan9(void);	/* that wsys is gone (libdraw's wsyslost) */
void	_glfwDrawErrstrPlan9(char *buf, int n);	/* the system's error string of the last failed call */
/* the window's contents on the screen, and the border around them */
void	_glfwDrawRectPlan9(_GLFWdrawwinPlan9*, int *x, int *y, int *w, int *h, int *border);
void	_glfwDrawScreenPlan9(int *w, int *h);	/* the whole screen */
int	_glfwDrawDpiPlan9(void);	/* the window system's dots per inch (wsys' scale ×96), as of the last attach */
