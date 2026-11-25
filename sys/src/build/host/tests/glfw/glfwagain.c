/*
 * SPDX-License-Identifier: MIT
 *
 * glfwagain: a GLFW window (no client API: it draws nothing) that outlives
 * its wsys (sys/src/build/host/tests/glfw/glfwagain.rc): events for secs seconds, the refresh
 * callback a line each ("event refresh": GLFW's plan9 platform asks for one
 * when the window is made again in the next wsys), whether it was closed.
 * "more" after secs: a second window too, made again with it in the next
 * wsys (libdraw's wsysopen).
 * Prints "ok ..." / "FAIL ..." lines and "glfwagain: all passed".
 */
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int refreshes;

static void
refresh(GLFWwindow *w)
{
    (void) w;
    refreshes++;
    printf("event refresh\n");
}

int
main(int argc, char **argv)
{
    GLFWwindow *w, *two = NULL;
    double t0;
    int secs;

    secs = argc > 1 ? atoi(argv[1]) : 20;
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (!glfwInit()) {
        printf("FAIL glfwInit\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if ((w = glfwCreateWindow(300, 200, "glfwagain", NULL, NULL)) == NULL) {
        printf("FAIL glfwCreateWindow\n");
        return 1;
    }
    glfwSetWindowRefreshCallback(w, refresh);
    printf("ok window\n");
    if (argc > 2 && strcmp(argv[2], "more") == 0) {
        if ((two = glfwCreateWindow(200, 120, "glfwagain two", NULL, NULL)) == NULL) {
            printf("FAIL a second window\n");
            return 1;
        }
        printf("ok more windows\n");
    }
    t0 = glfwGetTime();
    while (!glfwWindowShouldClose(w) && !(two && glfwWindowShouldClose(two)) && glfwGetTime() - t0 < secs)
        glfwWaitEventsTimeout(0.2);
    printf("ok %d refreshes\n", refreshes);
    if (glfwWindowShouldClose(w) || (two && glfwWindowShouldClose(two)))
        printf("FAIL it was closed\n");
    else
        printf("glfwagain: all passed\n");
    if (two)
        glfwDestroyWindow(two);
    glfwDestroyWindow(w);
    glfwTerminate();
    return 0;
}
