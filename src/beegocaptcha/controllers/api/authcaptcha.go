package api

import (
	"github.com/astaxie/beego"
)

type AuthcaptchaController struct {
	beego.Controller
}

func (this *AuthcaptchaController) Index() {
	this.Ctx.WriteString("auth captcha")
}
