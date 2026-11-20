// SPDX-License-Identifier: MIT
//
// ufs: the 9P file server the PXE laptops and the QEMU guests take their
// root from (scripts/hw/pxe.sh, scripts/guest/boot-cli.sh). Harvey's
// cmd/ufs (github.com/Harvey-OS/ninep, BSD-3-Clause: its LICENSE in vendor/),
// written anew around its filesystem package, with one difference: no TCP
// keepalive. net.Listen turns it on (Go: 15 s idle, then 9 probes 15 s
// apart), so the host dropped the connection of a laptop asleep for more
// than ~150 s, and its root with it (suspend to RAM). A client
// that is gone for good is noticed at its next request instead.
//
// Flags as Harvey's: -addr, -ntype, and the filesystem package's -root,
// -user, -debug. The module is vendored (go build -mod=vendor): no network
// at build time.
package main

import (
	"context"
	"flag"
	"log"
	"net"

	ufs "github.com/Harvey-OS/ninep/filesystem"
	"github.com/Harvey-OS/ninep/protocol"
)

var (
	ntype = flag.String("ntype", "tcp4", "network type")
	naddr = flag.String("addr", ":5640", "network address")
)

func main() {
	flag.Parse()

	lc := net.ListenConfig{KeepAlive: -1}	// no keepalive on the accepted connections
	ln, err := lc.Listen(context.Background(), *ntype, *naddr)
	if err != nil {
		log.Fatalf("listen: %v", err)
	}
	srv, err := ufs.NewUFS(func(l *protocol.Listener) error {
		l.Trace = nil
		return nil
	})
	if err != nil {
		log.Fatal(err)
	}
	if err := srv.Serve(ln); err != nil {
		log.Fatal(err)
	}
}
