package routers

import (
	// "beegocaptcha/controllers"
	"beegocaptcha/controllers/api"
	"github.com/astaxie/beego"
)

func init() {
	beego.Router("/api/getcaptcha", &api.GetcaptchaController{}, "*:Index")
	beego.Router("/api/authcaptcha", &api.AuthcaptchaController{}, "*:Index")
}
