package main

import (
	"database/sql"
	"fmt"
	_ "github.com/lib/pq"
	"math/rand"
	"time"
)

func main() {
	db, err := sql.Open("postgres", "user=root password=1q2w3e4r dbname=exampledb host=localhost sslmode=disable")
	checkErr(err)

	//查询数据
	rows, err := db.Query("SELECT * FROM user_geo")
	checkErr(err)

	for rows.Next() {
		var uid int
		var user_name string
		var geom string
		var created time.Time
		var updated time.Time
		err = rows.Scan(&uid, &user_name, &geom, &created, &updated)
		checkErr(err)
		fmt.Println(uid)
		fmt.Println(user_name)
		fmt.Println(geom)
		fmt.Println(created)
		fmt.Println(updated)
	}

	//插入数据
	stmt, err := db.Prepare("INSERT INTO user_geo(user_name,geom) VALUES($1, ST_GeomFromText($2, 4326)) RETURNING uid")
	checkErr(err)

	res, err := stmt.Exec("tommy", "POINT(10 21)")
	fmt.Println(res)
	checkErr(err)

	// //pg不支持这个函数
	// id, err := res.LastInsertId()
	// checkErr(err)

	// fmt.Println(id)

	//更新数据
	stmt, err = db.Prepare("update user_geo set user_name=$1 where uid=$2")
	checkErr(err)

	res, err = stmt.Exec("luck", 2)
	checkErr(err)

	affect, err := res.RowsAffected()
	checkErr(err)

	fmt.Println(affect)

	//查询数据
	rows, err = db.Query("SELECT * FROM user_geo")
	checkErr(err)

	for rows.Next() {
		var uid int
		var user_name string
		var geom string
		var created time.Time
		var updated time.Time
		err = rows.Scan(&uid, &user_name, &geom, &created, &updated)
		checkErr(err)
		fmt.Println(uid)
		fmt.Println(user_name)
		fmt.Println(geom)
		fmt.Println(created)
		fmt.Println(updated)
	}

	// 删除数据
	stmt, err = db.Prepare("delete from user_geo where uid=$1")
	checkErr(err)

	res, err = stmt.Exec(1)
	checkErr(err)

	affect, err = res.RowsAffected()
	checkErr(err)

	fmt.Println(affect)

	db.Close()

}

func getRandomLatLng() (lat, lng float64) {
	// var lat float64
	// var lng float64

	if rand.Seed(time.Now().Unix()); rand.Intn(2) == 0 {
		lng = (rand.Float64() * 170)
	} else {
		lng = (rand.Float64() * 170) - 170
	}
	if rand.Seed(time.Now().Unix() + 78); rand.Intn(2) == 0 {
		lat = (rand.Float64() * 80)
	} else {
		lat = (rand.Float64() * 80) - 80
	}

	return
}

func checkErr(err error) {
	if err != nil {
		panic(err)
	}
}
