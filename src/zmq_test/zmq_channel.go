package main

import (
	"log"

	"github.com/zeromq/goczmq"
)

func main() {
	// Create a router channeler and bind it to port 5555.
	// A channeler provides a thread safe channel interface
	// to a *Sock
	router := goczmq.NewRouterChanneler("tcp://*:5555")
	defer router.Destroy()

	log.Println("router created and bound")

	// Create a dealer channeler and connect it to the router.
	dealer := goczmq.NewDealerChanneler("tcp://127.0.0.1:5555")
	defer dealer.Destroy()

	log.Println("dealer created and connected")

	// Send a 'Hello' message from the dealer to the router.
	dealer.SendChan <- [][]byte{[]byte("Hello")}
	log.Println("dealer sent 'Hello'")

	// Receve the message as a [][]byte. Since this is
	// a router, the first frame of the message wil
	// be the routing frame.
	request := <-router.RecvChan
	log.Printf("router received '%s' from '%v'", request[1], request[0])

	// Send a reply. First we send the routing frame, which
	// lets the dealer know which client to send the message.
	router.SendChan <- [][]byte{request[0], []byte("World")}
	log.Printf("router sent 'World'")

	// Receive the reply.
	reply := <-dealer.RecvChan
	log.Printf("dealer received '%s'", string(reply[0]))
}
