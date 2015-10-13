package main

import (
	"database/sql"
	"fmt"
	_ "github.com/lib/pq"
	"math/rand"
	"strconv"
	"time"
)

const CHAN_BUFFER_SIZE = 8

type Geo struct {
	Lat float64
	Lng float64
}

func main() {
	db, err := sql.Open("postgres", "user=root password=1q2w3e4r dbname=exampledb host=localhost sslmode=disable")
	checkErr(err)

	Mchan := make(chan Geo, CHAN_BUFFER_SIZE)

	go func() {
		defer close(Mchan)

		for i := 0; i < 10000; i++ {
			lat, lng := getRandomLatLng()
			fmt.Println(lat, lng)
			geo := Geo{Lat: lat, Lng: lng}
			time.Sleep(time.Nanosecond)
			Mchan <- geo
		}

	}()

	for _geo := range Mchan {
		stmt, err := db.Prepare("INSERT INTO user_geo(user_name,geom) VALUES($1, ST_GeomFromText($2, 4326)) RETURNING uid")
		checkErr(err)

		name := time.Now().Unix()

		lat_str := strconv.FormatFloat(_geo.Lat, 'f', 14, 64)
		lng_str := strconv.FormatFloat(_geo.Lng, 'f', 14, 64)
		name_str := strconv.FormatInt(name, 10)

		// fmt.Println(name_str, lat_str, lng_str, "POINT("+lat_str+" "+lng_str+")")
		res, err := stmt.Exec("led-zeppelin-"+name_str, "POINT("+lat_str+" "+lng_str+")")
		fmt.Println(res)
		checkErr(err)

		if name%30 == 0 {
			time.Sleep(time.Second)
		}

	}
}

func getRandomLatLng() (lat, lng float64) {
	// var lat float64
	// var lng float64

	if rand.Seed(time.Now().UnixNano()); rand.Intn(2) == 0 {
		lat = (rand.Float64() * 170)
	} else {
		lat = (rand.Float64() * 170) - 170
	}
	if rand.Seed(time.Now().UnixNano() + 78); rand.Intn(2) == 0 {
		lng = (rand.Float64() * 80)
	} else {
		lng = (rand.Float64() * 80) - 80
	}

	return
}

func checkErr(err error) {
	if err != nil {
		panic(err)
	}
}
