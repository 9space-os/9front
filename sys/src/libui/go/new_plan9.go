// SPDX-License-Identifier: MIT

package ui

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"strconv"
	"syscall"
	"time"
)

// ORCLOSE of open(2): the file goes when its fd is closed (no syscall name)
const oRCLOSE = 64

// lifeline makes ui's lifeline (ui -c): a pipe, one end posted as
// /srv/name (ORCLOSE: the entry goes with this program), the other kept
// here; ui reads the posted end and gets end of file when this program
// ends, however it ends (or Close): it ends then too. Both close on exec:
// no program this one starts keeps ui alive.
func lifeline(name string) (func(), error) {
	var p [2]int
	if err := syscall.Pipe(p[:]); err != nil {
		return nil, err
	}
	srv, err := syscall.Create("/srv/"+name, syscall.O_WRONLY|oRCLOSE|syscall.O_CLOEXEC, 0600)
	if err == nil {
		_, err = syscall.Write(srv, []byte(strconv.Itoa(p[1])))
		if err != nil {
			syscall.Close(srv)
		}
	}
	syscall.Close(p[1])
	if err != nil {
		syscall.Close(p[0])
		return nil, err
	}
	end, err := syscall.Open("#d/"+strconv.Itoa(p[0]), syscall.O_RDONLY|syscall.O_CLOEXEC)
	syscall.Close(p[0])
	if err != nil {
		syscall.Close(srv)
		return nil, err
	}
	return func() { syscall.Close(srv); syscall.Close(end) }, nil
}

// the files mounted: the kernel's 9P, shared with whoever mounts the same
type mounted struct{ mtpt string }

func (m *mounted) open(name string, mode int) (io.ReadWriteCloser, error) {
	return os.OpenFile(m.mtpt+"/"+name, mode, 0)
}

func (m *mounted) close() error { return syscall.Unmount("", m.mtpt) }

// Open is the ui posted as /srv/srv, mounted on /mnt/srv.
func Open(srv string) (*Ui, error) {
	fd, err := syscall.Open("/srv/"+srv, syscall.O_RDWR)
	if err != nil {
		return nil, fmt.Errorf("/srv/%s: %v", srv, err)
	}
	// a mount that works closes fd (bind(2)): not an os.File, that would
	// close it again (and whatever has its number by then)
	mtpt := "/mnt/" + srv
	if err := syscall.Mount(fd, -1, mtpt, syscall.MREPL, ""); err != nil {
		syscall.Close(fd)
		return nil, fmt.Errorf("mount %s: %v", mtpt, err)
	}
	u, err := newUi(&mounted{mtpt})
	if err != nil {
		return nil, err
	}
	u.Srv = srv
	return u, nil
}

// New starts ui in a new window of rio or wsys (window(1)) and is its files.
func New(title string) (*Ui, error) {
	user := os.Getenv("user")
	if user == "" {
		user = "none"
	}
	srv := fmt.Sprintf("ui.%s.%d.0", user, os.Getpid())
	life, err := lifeline(srv + ".life")
	if err != nil {
		return nil, fmt.Errorf("/srv/%s.life: %v", srv, err)
	}
	// window returns once the window is there; ui posts its files soon after
	if err := exec.Command("/bin/window", "/bin/ui -c "+srv+".life -s "+srv).Run(); err != nil {
		life()
		return nil, fmt.Errorf("window: %v", err)
	}
	for t := time.Now(); ; time.Sleep(100 * time.Millisecond) {
		if _, err := os.Stat("/srv/" + srv); err == nil {
			break
		}
		if time.Since(t) > 10*time.Second {
			life()
			return nil, fmt.Errorf("no /srv/%s from a new ui", srv)
		}
	}
	u, err := Open(srv)
	if err != nil {
		life()
		return nil, err
	}
	u.life = life
	if err := u.Ctl("title %s\n", Quote(title)); err != nil {
		u.Close()
		return nil, err
	}
	return u, nil
}
