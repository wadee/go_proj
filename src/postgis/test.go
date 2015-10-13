package main

import (
	"fmt"
	"math/rand"
	"time"
)

func main() {
	// rand.Seed(time.Now().Unix())
	var lat float64
	var lng float64

	if rand.Seed(time.Now().Unix()); rand.Intn(2) == 0 {
		lng = (rand.Float64() * 180)
	} else {
		lng = (rand.Float64() * 180) - 180
	}
	if rand.Seed(time.Now().Unix() + 78); rand.Intn(2) == 0 {
		lat = (rand.Float64() * 90)
	} else {
		lat = (rand.Float64() * 90) - 90
	}

	fmt.Println(lat, lng)

}
