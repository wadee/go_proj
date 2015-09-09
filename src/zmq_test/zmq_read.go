package main

import (
	"log"

	"github.com/zeromq/goczmq"
)

func main() {
	// Create a router socket and bind it to port 5555.
	router, err := goczmq.NewRouter("tcp://*:5555")
	if err != nil {
		log.Fatal(err)
	}
	defer router.Destroy()

	log.Println("router created and bound")

	// Create a dealer socket and connect it to the router.
	dealer, err := goczmq.NewDealer("tcp://127.0.0.1:5555")
	if err != nil {
		log.Fatal(err)
	}
	defer dealer.Destroy()

	log.Println("dealer created and connected")

	// Send a 'Hello' message from the dealer to the router,
	// using the io.Write interface
	n, err := dealer.Write([]byte("Hello"))
	if err != nil {
		log.Fatal(err)
	}

	log.Printf("dealer sent %d byte message 'Hello'\n", n)

	// Make a byte slice and pass it to the router
	// Read interface. When using the ReadWriter
	// interface with a router socket, the router
	// caches the routing frames internally in a
	// FIFO and uses them transparently when
	// sending replies.
	buf := make([]byte, 16386)

	n, err = router.Read(buf)
	if err != nil {
		log.Fatal(err)
	}

	log.Printf("router received '%s'\n", buf[:n])

	// Send a reply.
	n, err = router.Write([]byte("World"))
	if err != nil {
		log.Fatal(err)
	}

	log.Printf("router sent %d byte message 'World'\n", n)

	// Receive the reply, reusing the previous buffer.
	n, err = dealer.Read(buf)
	if err != nil {
		log.Fatal(err)
	}

	log.Printf("dealer received '%s'", string(buf[:n]))
}
