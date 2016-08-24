package daemons

import (
	"fmt"
	"time"
)

type CaptchaDaemon struct {
}

func (this *CaptchaDaemon) Do() {
	for {
		fmt.Println("captcha do")
		time.Sleep(time.Second)
	}

}
