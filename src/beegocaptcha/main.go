package main

import (
	_ "beegocaptcha/routers"
	"github.com/astaxie/beego"

	"beegocaptcha/daemons"
)

func main() {
	captcha_daemon := &daemons.CaptchaDaemon{}
	daemons.NewRunner(captcha_daemon).Run()
	beego.Run()
}
