package api

import (
	"github.com/astaxie/beego"
)

type GetcaptchaController struct {
	beego.Controller
}

func (this *GetcaptchaController) Index() {
	this.Ctx.WriteString("Get Captcha")
}
