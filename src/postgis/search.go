package main

import (
	"database/sql"
	// "fmt"
	_ "github.com/lib/pq"
	"math/rand"
	"strconv"
	"sync"
	"time"
)

func main() {

	db, err := sql.Open("postgres", "user=root password=1q2w3e4r dbname=exampledb host=localhost sslmode=disable")
	checkErr(err)

	//查询数据
	var wait sync.WaitGroup

	count := 5
	wait.Add(count)

	for i := 0; i < count; i++ {
		go func() {
			for i := 0; i < 200000; i++ {
				lat, lng := getRandomLatLng()

				point := `POINT(` + strconv.FormatFloat(lat, 'f', 14, 64) + ` ` + strconv.FormatFloat(lng, 'f', 14, 64) + `)`
				distance := `0.002`

				sql := `select * , ST_Distance(ST_Transform(geom, 26986), ST_Transform(ST_GeometryFromText('` + point + `', 4326), 26986)) as distance from user_geo where ST_DWithin(geom, ST_GeometryFromText('` + point + `', 4326), ` + distance + `) LIMIT 10 offset 0;`

				// fmt.Println(sql)

				rows, err := db.Query(sql)
				checkErr(err)

				for rows.Next() {
					var uid int
					var user_name string
					var geom string
					var created time.Time
					var updated time.Time
					var distance float64
					err = rows.Scan(&uid, &user_name, &geom, &created, &updated, &distance)
					checkErr(err)
					// fmt.Println(uid)
					// fmt.Println(user_name)
					// fmt.Println(geom)
					// fmt.Println(created)
					// fmt.Println(updated)
					// fmt.Println(distance)
				}
				time.Sleep(time.Microsecond)
			}
			wait.Done()
		}()
	}

	wait.Wait()

}

func checkErr(err error) {
	if err != nil {
		panic(err)
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
