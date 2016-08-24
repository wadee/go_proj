package daemons

import (
	// "fmt"
	"time"

	"beegocaptcha/interfaces"
)

type DaemonsRunner struct {
	daemon_unit interfaces.DaemonUnit
}

func NewRunner(daemon_unit interfaces.DaemonUnit) *DaemonsRunner {
	daemons_runner := &DaemonsRunner{daemon_unit: daemon_unit}
	return daemons_runner
}

func (this *DaemonsRunner) Run() {
	go this.daemon_unit.Do()
}
