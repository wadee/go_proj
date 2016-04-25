package models

import (
	"errors"
	"strconv"
	"time"

	"database/sql"
	_ "github.com/go-sql-driver/mysql"
	// "log"
)

var (
	db_instance *sql.DB
)

func init() {
	host := beego.AppConfig.String("db_host")
	port := beego.AppConfig.String("db_port")
	user := beego.AppConfig.String("db_user")
	password := beego.AppConfig.String("db_password")
	database := beego.AppConfig.String("db_database")

	db_instance = connect(host, port, user, password, database)
}

func connect(host, port, user, password, database string) *sql.DB {
	conn := user + ":" + password + "@tcp(" + host + ":" + port + ")/" + database + "?charset=utf8"
	db, err := sql.Open("mysql", conn)
	if err != nil {
		panic("db connect fail")
	}
	return db
}

type User struct {
	Id  string
	Url string
}

func AddUserUrl(u User) bool {
	sql := "REPLACE INTO `user_url` ( `user_id`, `url` ) values ( ? , ? )"
	ret := db_instance.Exec(sql, u.Id, u.Url)
	fmt.Println(ret)
	return true
}

// func GetUser(uid string) (u *User, err error) {
// 	if u, ok := UserList[uid]; ok {
// 		return u, nil
// 	}
// 	return nil, errors.New("User not exists")
// }

// func GetAllUsers() map[string]*User {
// 	return UserList
// }

// func UpdateUser(uid string, uu *User) (a *User, err error) {
// 	if u, ok := UserList[uid]; ok {
// 		if uu.Username != "" {
// 			u.Username = uu.Username
// 		}
// 		if uu.Password != "" {
// 			u.Password = uu.Password
// 		}
// 		if uu.Profile.Age != 0 {
// 			u.Profile.Age = uu.Profile.Age
// 		}
// 		if uu.Profile.Address != "" {
// 			u.Profile.Address = uu.Profile.Address
// 		}
// 		if uu.Profile.Gender != "" {
// 			u.Profile.Gender = uu.Profile.Gender
// 		}
// 		if uu.Profile.Email != "" {
// 			u.Profile.Email = uu.Profile.Email
// 		}
// 		return u, nil
// 	}
// 	return nil, errors.New("User Not Exist")
// }

// func Login(username, password string) bool {
// 	for _, u := range UserList {
// 		if u.Username == username && u.Password == password {
// 			return true
// 		}
// 	}
// 	return false
// }

// func DeleteUser(uid string) {
// 	delete(UserList, uid)
// }
