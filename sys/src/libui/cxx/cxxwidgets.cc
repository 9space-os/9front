// SPDX-License-Identifier: MIT
//
// cxxwidgets: every widget of ui(4) in a window, from C++ (ui.hh); the same
// scene as rcwidgets (rc), gowidgets (Go) and uiwidgets (Rust). A switch
// makes the desktop dark or light (the theme's mode, /dev/theme); a dialog,
// a second window and a popup menu are windows of wsys (ui's window nodes);
// what the user does shows in the status line. Prints "ready srv" once its
// scene is there, then an event a line.
// The image is 9front's face, read at run time from /lib/face (9front's,
// MIT: /lib/legal/mit).
#include <ui.hh>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

using ui::Tx;

static const char svg[] =
	"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\">"
	"<circle cx=\"32\" cy=\"32\" r=\"30\" fill=\"#7a5a28\"/>"
	"<circle cx=\"32\" cy=\"32\" r=\"18\" fill=\"#eeee9e\"/>"
	"<circle cx=\"32\" cy=\"32\" r=\"6\" fill=\"#4a6628\"/></svg>";

static const char prose[] =
	"Prose: text to select and copy, not to edit; button 3 plumbs it. It sits in a scroll, "
	"which moves it when it is longer than the room it has: the wheel, or the bar at its side. "
	"Plan 9 takes text as the stuff of work: what is shown can be selected, sent and plumbed, "
	"and the window is a file.";

// twenty rows of the long list for page p, those of the page before gone
static void rows(ui::Ui &u, int p, bool again)
{
	Tx t;
	for(int i = 1; i <= 20; i++){
		std::string id = "row" + std::to_string(i);
		if(again)
			t.line("del " + id);
		t.node(id, "label", "ls", "text", "row " + std::to_string(p * 20 + i));
	}
	t.send(u);
}

static void scene(ui::Ui &u)
{
	Tx t;
	// its rows across the window: the head's spacer puts the switch at the right
	t.set("root", "padding", "16", "gap", "12", "align", "stretch");
	t.node("head", "row", "root", "gap", "12", "align", "center");
	t.node("h1", "varlabel", "head", "text", "9space widgets", "weight", "700");
	t.node("lang", "badge", "head", "text", "C++");
	t.node("fill", "label", "head", "text", " ", "flex", "1");
	t.node("darkl", "label", "head", "text", "dark");
	t.node("dark", "switch", "head");
	t.node("sect", "row", "root", "gap", "8");
	const char *sects[] = {"Controls", "Text", "Lists", "Layout"};
	for(int i = 0; i < 4; i++)
		t.node("s" + std::to_string(i), "button", "sect", "text", sects[i]);
	t.node("d0", "divider", "root");
	t.node("st", "stack", "root", "flex", "1", "active", "0");
	t.send(u);

	// Controls
	t.node("c0", "column", "st", "gap", "12");
	t.node("r1", "row", "c0", "gap", "8");
	t.node("press", "button", "r1", "text", "Press me");
	t.node("bdlg", "button", "r1", "text", "Dialog…");
	t.node("bwin", "button", "r1", "text", "Window…");
	t.node("bmenu", "button", "r1", "text", "Menu…");
	t.node("r2", "row", "c0", "gap", "16", "align", "center");
	t.node("ck", "checkbox", "r2", "text", "Check me");
	t.node("sw", "switch", "r2");
	t.node("swl", "label", "r2", "text", "a switch");
	t.node("rg", "radiogroup", "c0");
	t.node("rgc", "row", "rg", "gap", "16");
	t.node("ra", "radio", "rgc", "text", "Umber", "checked", "1");
	t.node("rb", "radio", "rgc", "text", "Ochre");
	t.node("rc", "radio", "rgc", "text", "Sand");
	t.node("r3", "row", "c0", "gap", "12", "align", "center");
	t.node("sll", "label", "r3", "text", "Slider");
	t.node("sl", "slider", "r3", "min", "0", "max", "1", "value", "0.3", "flex", "1");
	t.node("pr", "progress", "r3", "value", "0.3", "flex", "1");
	t.node("spn", "spinner", "r3");
	t.node("r4", "row", "c0", "gap", "12", "align", "center");
	t.node("stl", "label", "r4", "text", "Stepper");
	t.node("stp", "stepper", "r4", "min", "0", "max", "10", "value", "3");
	t.node("sel", "select", "r4", "options", "Umber|Ochre|Sand|Moss", "selected", "0");
	t.node("bdg", "badged", "r4", "badge", "3");
	t.node("inbox", "button", "bdg", "text", "Inbox");
	t.node("cnt", "badge", "r4", "count", "12");
	t.send(u);

	// Text
	t.node("c1", "column", "st", "gap", "12");
	t.node("tb", "textbox", "c1", "placeholder", "type here");
	t.node("echo", "label", "c1", "text", "(what you type)");
	t.node("ta", "textarea", "c1", "placeholder", "lines of text: Enter makes a new one", "flex", "1");
	t.node("prs", "scroll", "c1", "flex", "1");
	t.node("pro", "prose", "prs", "text", prose);
	t.node("r5", "row", "c1", "gap", "12", "align", "center");
	t.node("vl", "varlabel", "r5", "text", "weight", "weight", "400", "animate", "200");
	t.node("wsl", "slider", "r5", "min", "100", "max", "900", "value", "400", "flex", "1");
	t.send(u);

	// Lists
	t.node("c2", "column", "st", "gap", "12");
	t.node("g", "grid", "c2", "columns", "3");
	const char *cells[] = {"Colour", "Hex", "Use", "Umber", "#6b5a3a", "border", "Ochre", "#7a5a28", "accent"};
	for(int i = 0; i < 9; i++)
		t.node("g" + std::to_string(i), "label", "g", "text", cells[i]);
	t.node("col", "collapse", "c2", "text", "More");
	t.node("colt", "label", "col", "text", "folded away until asked");
	t.node("pg", "pages", "c2", "count", "5", "page", "0");
	t.node("ls", "list", "c2", "flex", "1");
	t.send(u);
	rows(u, 0, false);

	// Layout
	t.node("c3", "column", "st", "gap", "12");
	t.node("spl", "split", "c3", "fraction", "0.35", "draggable", "1", "flex", "1");
	t.node("left", "label", "spl", "text", "left of the split (drag the bar)");
	t.node("right", "column", "spl", "gap", "8");
	t.node("al", "align", "right", "align", "center");
	t.node("alt", "label", "al", "text", "centred");
	t.node("d1", "divider", "right");
	t.node("zs", "zstack", "right", "align", "center");
	t.node("zbox", "box", "zs", "width", "160", "height", "48", "background", "#d8cfb2", "radius", "8");
	t.node("ztext", "label", "zs", "text", "over a box");
	t.node("pics", "row", "right", "gap", "12", "align", "center");
	t.node("logo", "svg", "pics");
	t.node("img", "image", "pics");
	t.node("status", "label", "root", "text", "ready");
	t.send(u);
	std::ifstream f("/lib/face/48x48x8/n/9front.1", std::ios::binary);
	if(f){
		std::stringstream b;
		b << f.rdbuf();
		u.write_file("nodes/img/image", b.str());
	}
	u.write_file("nodes/logo/image", svg);
}

static void dialog(ui::Ui &u)
{
	Tx t;
	t.node("dlg", "window", "root", "role", "dialog", "modal", "1", "title", "A dialog", "width", "320", "height", "120");
	t.node("dlgc", "column", "dlg", "padding", "16", "gap", "12");
	t.node("dlgt", "label", "dlgc", "text", "Discard the changes?");
	t.node("dlgr", "row", "dlgc", "gap", "8");
	t.node("dcancel", "button", "dlgr", "text", "Cancel");
	t.node("dok", "button", "dlgr", "text", "Discard");
	t.send(u);
}

static void second(ui::Ui &u)
{
	Tx t;
	t.node("w2", "window", "root", "role", "window", "title", "A second window", "width", "300", "height", "160");
	t.node("w2c", "column", "w2", "padding", "16", "gap", "12");
	t.node("w2t", "label", "w2c", "text", "A window of its own");
	t.node("w2close", "button", "w2c", "text", "Close");
	t.send(u);
}

static std::vector<int> nums(const std::string &s)
{
	std::istringstream in(s);
	std::vector<int> v;
	int n;
	while(in >> n)
		v.push_back(n);
	return v;
}

// under the menu button: ui's window on the screen and the button in it
static void menu(ui::Ui &u)
{
	auto o = nums(u.read_file("ctl")), r = nums(u.read_file("nodes/bmenu/rect"));
	if(o.size() < 2 || r.size() < 4)
		return;
	Tx t;
	t.node("pop", "window", "root", "role", "popup", "x", std::to_string(o[0] + r[0]),
		"y", std::to_string(o[1] + r[3]), "width", "140", "height", "108");
	t.node("popc", "column", "pop", "padding", "4", "gap", "2");
	for(const char *s : {"Umber", "Ochre", "Sand"})
		t.node(std::string("m") + s, "button", "popc", "text", s);
	t.send(u);
}

int main()
{
	try{
		ui::Ui u("widgets: C++");
		scene(u);
		printf("ready %s\n", u.srv.c_str());
		fflush(stdout);
		auto status = [&](const std::string &s){ u.set("status", "text", s); };
		auto gone = [&](const std::string &id){
			try{ u.ctl("del " + id + "\n"); }catch(const std::exception&){}
		};
		ui::Event e;
		while(u.event(e)){
			printf("event %s %s %s\n", e.kind.c_str(), e.id.c_str(), e.arg.c_str());
			fflush(stdout);
			if(e.kind == "close"){
				if(e.id.empty())
					break;
				gone(e.id);
				status("closed " + e.id);
			}else if(e.kind == "click"){
				const std::string &id = e.id;
				if(id == "s0" || id == "s1" || id == "s2" || id == "s3")
					u.set("st", "active", id.substr(1));
				else if(id == "bdlg" && !u.exists("dlg"))
					dialog(u);
				else if(id == "bwin" && !u.exists("w2"))
					second(u);
				else if(id == "bmenu" && !u.exists("pop"))
					menu(u);
				else if(id == "dok" || id == "dcancel")
					gone("dlg");
				else if(id == "w2close")
					gone("w2");
				else if(id == "mUmber" || id == "mOchre" || id == "mSand")
					gone("pop");
				status("clicked " + id);
			}else if(e.kind == "check"){
				if(e.id == "dark"){
					std::ofstream th("/dev/theme");
					th << "mode " << (e.arg == "1" ? "dark" : "light") << "\n";
				}
				status(e.id + " " + e.arg);
			}else if(e.kind == "slide"){
				if(e.id == "sl")
					u.set("pr", "value", e.arg);
				else if(e.id == "wsl")
					u.set("vl", "weight", e.arg);
				status(e.id + " " + e.arg);
			}else if(e.kind == "text"){
				if(e.id == "tb")
					u.set("echo", "text", e.arg);
			}else if(e.kind == "page"){
				rows(u, atoi(e.arg.c_str()), true);
				status("page " + e.arg);
			}else
				status(e.kind + " " + e.id + " " + e.arg);
		}
	}catch(const std::exception &x){
		fprintf(stderr, "cxxwidgets: %s\n", x.what());
		return 1;
	}
	return 0;
}
