package controllers

import (
	"bookmarkapi/models"
	"encoding/json"
	"fmt"

	"github.com/astaxie/beego"
)

// Operations about Users
type BookmarkcollectionController struct {
	beego.Controller
}

// @Title createUser
// @Description create users
// @Param	body		body 	models.User	true		"body for user content"
// @Success 200 {int} models.User.Id
// @Failure 403 body is empty
// @router / [post]
func (bmc *BookmarkcollectionController) Post() {
	var bookmarkcollection = make([]models.Bookmark, 0)
	jsoninfo := bmc.GetString("bookmark_list")
	// fmt.Println(jsoninfo)
	// var bookmark models.Bookmark
	json.Unmarshal([]byte(jsoninfo), &bookmarkcollection)
	for _, bm := range bookmarkcollection {
		fmt.Println(bm.Url, bm.Title)
	}
}

// @Title Get
// @Description get all Users
// @Success 200 {object} models.User
// @router / [get]
func (u *BookmarkcollectionController) GetAll() {
	users := models.GetAllUsers()
	u.Data["json"] = users
	u.ServeJson()
}

// @Title Get
// @Description get user by uid
// @Param	uid		path 	string	true		"The key for staticblock"
// @Success 200 {object} models.User
// @Failure 403 :uid is empty
// @router /:uid [get]
func (u *BookmarkcollectionController) Get() {
	uid := u.GetString(":uid")
	if uid != "" {
		user, err := models.GetUser(uid)
		if err != nil {
			u.Data["json"] = err
		} else {
			u.Data["json"] = user
		}
	}
	u.ServeJson()
}

// @Title update
// @Description update the user
// @Param	uid		path 	string	true		"The uid you want to update"
// @Param	body		body 	models.User	true		"body for user content"
// @Success 200 {object} models.User
// @Failure 403 :uid is not int
// @router /:uid [put]
func (u *BookmarkcollectionController) Put() {
	uid := u.GetString(":uid")
	if uid != "" {
		var user models.User
		json.Unmarshal(u.Ctx.Input.RequestBody, &user)
		uu, err := models.UpdateUser(uid, &user)
		if err != nil {
			u.Data["json"] = err
		} else {
			u.Data["json"] = uu
		}
	}
	u.ServeJson()
}

// @Title delete
// @Description delete the user
// @Param	uid		path 	string	true		"The uid you want to delete"
// @Success 200 {string} delete success!
// @Failure 403 uid is empty
// @router /:uid [delete]
func (u *BookmarkcollectionController) Delete() {
	uid := u.GetString(":uid")
	models.DeleteUser(uid)
	u.Data["json"] = "delete success!"
	u.ServeJson()
}

// @Title login
// @Description Logs user into the system
// @Param	username		query 	string	true		"The username for login"
// @Param	password		query 	string	true		"The password for login"
// @Success 200 {string} login success
// @Failure 403 user not exist
// @router /login [get]
func (u *BookmarkcollectionController) Login() {
	username := u.GetString("username")
	password := u.GetString("password")
	if models.Login(username, password) {
		u.Data["json"] = "login success"
	} else {
		u.Data["json"] = "user not exist"
	}
	u.ServeJson()
}

// @Title logout
// @Description Logs out current logged in user session
// @Success 200 {string} logout success
// @router /logout [get]
func (u *BookmarkcollectionController) Logout() {
	u.Data["json"] = "logout success"
	u.ServeJson()
}
