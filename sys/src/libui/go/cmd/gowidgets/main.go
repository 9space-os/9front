// SPDX-License-Identifier: MIT

//go:build plan9

// gowidgets: every widget of ui(4) in a window, from Go (package ui); the
// same scene as rcwidgets (rc), uiwidgets (Rust) and cxxwidgets (C++). A
// switch makes the desktop dark or light (the theme's mode, /dev/theme); a
// dialog, a second window and a popup menu are windows of wsys (ui's window
// nodes); what the user does shows in the status line. Prints "ready srv"
// once its scene is there, then an event a line.
// The image is 9front's face, read at run time from /lib/face (9front's,
// MIT: /lib/legal/mit).
package main

import (
	"fmt"
	"log"
	"os"
	"strconv"
	"strings"

	"ui"
)

// a transaction: the lines of one write
type tx struct{ strings.Builder }

// node id of kind in parent, its properties key, value, ...
func (t *tx) node(id, kind, parent string, kv ...string) {
	fmt.Fprintf(t, "new %s %s %s\n", id, kind, parent)
	t.set(id, kv...)
}

func (t *tx) set(id string, kv ...string) {
	for i := 0; i+1 < len(kv); i += 2 {
		fmt.Fprintf(t, "set %s %s %s\n", id, kv[i], ui.Quote(kv[i+1]))
	}
}

func (t *tx) send(u *ui.Ui) {
	if err := u.Ctl("%s", t.String()); err != nil {
		log.Fatal(err)
	}
	t.Reset()
}

const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><circle cx="32" cy="32" r="30" fill="#7a5a28"/><circle cx="32" cy="32" r="18" fill="#eeee9e"/><circle cx="32" cy="32" r="6" fill="#4a6628"/></svg>`

const prose = "Prose: text to select and copy, not to edit; button 3 plumbs it. It sits in a scroll, which moves it when it is longer than the room it has: the wheel, or the bar at its side. Plan 9 takes text as the stuff of work: what is shown can be selected, sent and plumbed, and the window is a file."

func scene(u *ui.Ui) {
	var t tx
	t.WriteString("title widgets: Go\n")
	// its rows across the window: the head's spacer puts the switch at the right
	t.set("root", "padding", "16", "gap", "12", "align", "stretch")
	t.node("head", "row", "root", "gap", "12", "align", "center")
	t.node("h1", "varlabel", "head", "text", "9space widgets", "weight", "700")
	t.node("lang", "badge", "head", "text", "Go")
	t.node("fill", "label", "head", "text", " ", "flex", "1")
	t.node("darkl", "label", "head", "text", "dark")
	t.node("dark", "switch", "head")
	t.node("sect", "row", "root", "gap", "8")
	for i, s := range []string{"Controls", "Text", "Lists", "Layout"} {
		t.node(fmt.Sprintf("s%d", i), "button", "sect", "text", s)
	}
	t.node("d0", "divider", "root")
	t.node("st", "stack", "root", "flex", "1", "active", "0")
	t.send(u)

	// Controls
	t.node("c0", "column", "st", "gap", "12")
	t.node("r1", "row", "c0", "gap", "8")
	t.node("press", "button", "r1", "text", "Press me")
	t.node("bdlg", "button", "r1", "text", "Dialog…")
	t.node("bwin", "button", "r1", "text", "Window…")
	t.node("bmenu", "button", "r1", "text", "Menu…")
	t.node("r2", "row", "c0", "gap", "16", "align", "center")
	t.node("ck", "checkbox", "r2", "text", "Check me")
	t.node("sw", "switch", "r2")
	t.node("swl", "label", "r2", "text", "a switch")
	t.node("rg", "radiogroup", "c0")
	t.node("rgc", "row", "rg", "gap", "16")
	t.node("ra", "radio", "rgc", "text", "Umber", "checked", "1")
	t.node("rb", "radio", "rgc", "text", "Ochre")
	t.node("rc", "radio", "rgc", "text", "Sand")
	t.node("r3", "row", "c0", "gap", "12", "align", "center")
	t.node("sll", "label", "r3", "text", "Slider")
	t.node("sl", "slider", "r3", "min", "0", "max", "1", "value", "0.3", "flex", "1")
	t.node("pr", "progress", "r3", "value", "0.3", "flex", "1")
	t.node("spn", "spinner", "r3")
	t.node("r4", "row", "c0", "gap", "12", "align", "center")
	t.node("stl", "label", "r4", "text", "Stepper")
	t.node("stp", "stepper", "r4", "min", "0", "max", "10", "value", "3")
	t.node("sel", "select", "r4", "options", "Umber|Ochre|Sand|Moss", "selected", "0")
	t.node("bdg", "badged", "r4", "badge", "3")
	t.node("inbox", "button", "bdg", "text", "Inbox")
	t.node("cnt", "badge", "r4", "count", "12")
	t.send(u)

	// Text
	t.node("c1", "column", "st", "gap", "12")
	t.node("tb", "textbox", "c1", "placeholder", "type here")
	t.node("echo", "label", "c1", "text", "(what you type)")
	t.node("ta", "textarea", "c1", "placeholder", "lines of text: Enter makes a new one", "flex", "1")
	t.node("prs", "scroll", "c1", "flex", "1")
	t.node("pro", "prose", "prs", "text", prose)
	t.node("r5", "row", "c1", "gap", "12", "align", "center")
	t.node("vl", "varlabel", "r5", "text", "weight", "weight", "400", "animate", "200")
	t.node("wsl", "slider", "r5", "min", "100", "max", "900", "value", "400", "flex", "1")
	t.send(u)

	// Lists
	t.node("c2", "column", "st", "gap", "12")
	t.node("g", "grid", "c2", "columns", "3")
	for i, s := range []string{"Colour", "Hex", "Use", "Umber", "#6b5a3a", "border", "Ochre", "#7a5a28", "accent"} {
		t.node(fmt.Sprintf("g%d", i), "label", "g", "text", s)
	}
	t.node("col", "collapse", "c2", "text", "More")
	t.node("colt", "label", "col", "text", "folded away until asked")
	t.node("pg", "pages", "c2", "count", "5", "page", "0")
	t.node("ls", "list", "c2", "flex", "1")
	t.send(u)
	rows(u, 0, false)

	// Layout
	t.node("c3", "column", "st", "gap", "12")
	t.node("spl", "split", "c3", "fraction", "0.35", "draggable", "1", "flex", "1")
	t.node("left", "label", "spl", "text", "left of the split (drag the bar)")
	t.node("right", "column", "spl", "gap", "8")
	t.node("al", "align", "right", "align", "center")
	t.node("alt", "label", "al", "text", "centred")
	t.node("d1", "divider", "right")
	t.node("zs", "zstack", "right", "align", "center")
	t.node("zbox", "box", "zs", "width", "160", "height", "48", "background", "#d8cfb2", "radius", "8")
	t.node("ztext", "label", "zs", "text", "over a box")
	t.node("pics", "row", "right", "gap", "12", "align", "center")
	t.node("logo", "svg", "pics")
	t.node("img", "image", "pics")
	t.node("status", "label", "root", "text", "ready")
	t.send(u)
	if b, err := os.ReadFile("/lib/face/48x48x8/n/9front.1"); err == nil {
		u.WriteFile("nodes/img/image", b)
	}
	u.WriteFile("nodes/logo/image", []byte(svg))
}

// twenty rows of the long list for page p, those of the page before gone
func rows(u *ui.Ui, p int, again bool) {
	var t tx
	for i := 1; i <= 20; i++ {
		id := fmt.Sprintf("row%d", i)
		if again {
			fmt.Fprintf(&t, "del %s\n", id)
		}
		t.node(id, "label", "ls", "text", fmt.Sprintf("row %d", p*20+i))
	}
	t.send(u)
}

func dialog(u *ui.Ui) {
	var t tx
	t.node("dlg", "window", "root", "role", "dialog", "modal", "1", "title", "A dialog", "width", "320", "height", "120")
	t.node("dlgc", "column", "dlg", "padding", "16", "gap", "12")
	t.node("dlgt", "label", "dlgc", "text", "Discard the changes?")
	t.node("dlgr", "row", "dlgc", "gap", "8")
	t.node("dcancel", "button", "dlgr", "text", "Cancel")
	t.node("dok", "button", "dlgr", "text", "Discard")
	t.send(u)
}

func second(u *ui.Ui) {
	var t tx
	t.node("w2", "window", "root", "role", "window", "title", "A second window", "width", "300", "height", "160")
	t.node("w2c", "column", "w2", "padding", "16", "gap", "12")
	t.node("w2t", "label", "w2c", "text", "A window of its own")
	t.node("w2close", "button", "w2c", "text", "Close")
	t.send(u)
}

// under the menu button: ui's window on the screen and the button in it
func menu(u *ui.Ui) {
	nums := func(name string) []int {
		b, _ := u.ReadFile(name)
		var v []int
		for _, f := range strings.Fields(string(b)) {
			n, _ := strconv.Atoi(f)
			v = append(v, n)
		}
		return v
	}
	o, r := nums("ctl"), nums("nodes/bmenu/rect")
	if len(o) < 2 || len(r) < 4 {
		return
	}
	var t tx
	t.node("pop", "window", "root", "role", "popup", "x", strconv.Itoa(o[0]+r[0]), "y", strconv.Itoa(o[1]+r[3]), "width", "140", "height", "108")
	t.node("popc", "column", "pop", "padding", "4", "gap", "2")
	for _, s := range []string{"Umber", "Ochre", "Sand"} {
		t.node("m"+s, "button", "popc", "text", s)
	}
	t.send(u)
}

func exists(u *ui.Ui, id string) bool {
	_, err := u.ReadFile("nodes/" + id + "/kind")
	return err == nil
}

func main() {
	log.SetFlags(0)
	log.SetPrefix("gowidgets: ")
	u, err := ui.New("widgets: Go")
	if err != nil {
		log.Fatal(err)
	}
	scene(u)
	fmt.Println("ready", u.Srv)
	status := func(s string) { u.Set("status", "text", s) }
	gone := func(id string) { u.Ctl("del %s\n", id) }
	for {
		e, err := u.Event()
		if err != nil {
			break
		}
		fmt.Println("event", e.Kind, e.ID, e.Arg)
		switch e.Kind {
		case "close":
			if e.ID == "" {
				os.Exit(0)
			}
			gone(e.ID)
			status("closed " + e.ID)
		case "click":
			switch e.ID {
			case "s0", "s1", "s2", "s3":
				u.Set("st", "active", e.ID[1:])
			case "bdlg":
				if !exists(u, "dlg") {
					dialog(u)
				}
			case "bwin":
				if !exists(u, "w2") {
					second(u)
				}
			case "bmenu":
				if !exists(u, "pop") {
					menu(u)
				}
			case "dok", "dcancel":
				gone("dlg")
			case "w2close":
				gone("w2")
			case "mUmber", "mOchre", "mSand":
				gone("pop")
			}
			status("clicked " + e.ID)
		case "check":
			if e.ID == "dark" {
				mode := "light"
				if e.Arg == "1" {
					mode = "dark"
				}
				if f, err := os.OpenFile("/dev/theme", os.O_WRONLY, 0); err == nil {
					f.WriteString("mode " + mode + "\n")
					f.Close()
				}
			}
			status(e.ID + " " + e.Arg)
		case "slide":
			switch e.ID {
			case "sl":
				u.Set("pr", "value", e.Arg)
			case "wsl":
				u.Set("vl", "weight", e.Arg)
			}
			status(e.ID + " " + e.Arg)
		case "text":
			if e.ID == "tb" {
				u.Set("echo", "text", e.Arg)
			}
		case "page":
			p, _ := strconv.Atoi(e.Arg)
			rows(u, p, true)
			status("page " + e.Arg)
		default:
			status(strings.TrimSpace(e.Kind + " " + e.ID + " " + e.Arg))
		}
	}
	u.Close()
}
