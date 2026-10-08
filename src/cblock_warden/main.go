/*-
 * Copyright (c) 2020 Christian S.J. Peron
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
package main

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"strings"

	"github.com/spf13/pflag"
	"gopkg.in/yaml.v3"
)

type CmdArgs struct {
	ManifestPath *string
	Prefix       *string
	DryRun       *bool
}

type PortMapping struct {
	HostPort      string `yaml:"host_port"`
	ContainerPort string `yaml:"container_port"`
	Public        bool   `yaml:"public"`
}

type Volume struct {
	FsType     string `yaml:"type"`
	Origin     string `yaml:"origin"`
	MountPoint string `yaml:"mountpoint"`
	Perms      string `yaml:"perms"`
}

type Cellblock struct {
	Image   string        `yaml:"image"`
	Network string        `yaml:"network"`
	Fdescfs bool          `yaml:"fdescfs"`
	Procfs  bool          `yaml:"procfs"`
	Tmpfs   bool          `yaml:"tmpfs"`
	Volumes []Volume      `yaml:"volumes"`
	Ports   []PortMapping `yaml:"ports"`
}

type CmdVec struct {
	Args []string
}

func (c *CmdVec) SetProg(path string) {
	c.Args = make([]string, 0)
	c.Args = append(c.Args, path)
}

func (c *CmdVec) AddBool(option string) {
	comp := "--" + option
	c.Args = append(c.Args, comp)
}

func (c *CmdVec) AddOption(option, value string) {
	c.Args = append(c.Args, "--"+option, value)
}

func (c *CmdVec) AddString(option string) {
	c.Args = append(c.Args, option)
}

type Config struct {
	Cellblocks []Cellblock `yaml:"cellblocks"`
}

func processPorts(portMappings []PortMapping, cmdLine *CmdVec) {
	for _, port := range portMappings {
		arg := ""
		if port.Public {
			arg = fmt.Sprintf("%s:%s:public", port.HostPort,
				port.ContainerPort)
		} else {
			arg = fmt.Sprintf("%s:%s", port.HostPort,
				port.ContainerPort)
		}
		cmdLine.AddOption("port", arg)
	}
}

func processVolumes(vols []Volume, cmdLine *CmdVec) {
	for _, vol := range vols {
		arg := fmt.Sprintf("%s:%s:%s:%s", vol.FsType, vol.Origin,
			vol.MountPoint, vol.Perms)
		cmdLine.AddOption("volume", arg)
	}
}

func ProcessManifest(gcfg Config, prog string) ([]CmdVec, error) {
	cmdvec := make([]CmdVec, 0)
	if len(gcfg.Cellblocks) == 0 {
		return cmdvec, fmt.Errorf("no cellblocks defined in config")
	}
	cellblockCount := 1
	for _, cb := range gcfg.Cellblocks {
		cmd := CmdVec{}
		cmd.SetProg(prog)
		cmd.AddString("launch")
		if cb.Image == "" {
			return cmdvec, fmt.Errorf("block number %d has no image\n", cellblockCount)
		}
		cmd.AddBool("no-attach")
		cmd.AddOption("name", cb.Image)
		if cb.Network != "" {
			cmd.AddOption("network", cb.Network)
		} else if len(cb.Ports) > 0 {
			return cmdvec, fmt.Errorf("block number %d has port mappings but no network, port mappings are not supported with host networking\n", cellblockCount)
		}
		if cb.Fdescfs {
			cmd.AddBool("fdescfs")
		}
		if cb.Procfs {
			cmd.AddBool("procfs")
		}
		if cb.Tmpfs {
			cmd.AddBool("tmpfs")
		}
		if len(cb.Volumes) > 0 {
			processVolumes(cb.Volumes, &cmd)
		}
		if len(cb.Ports) > 0 {
			processPorts(cb.Ports, &cmd)
		}
		cmdvec = append(cmdvec, cmd)
		cellblockCount++
	}
	return cmdvec, nil
}

// LaunchCellblocks runs cblock launch for each cellblock in the manifest,
// in order. A failed launch does not stop the others. It returns the
// number of launches that failed. With dryRun it only prints the commands.
func LaunchCellblocks(yamlData []byte, prefix string, dryRun bool) int {
	var gcfg Config

	err := yaml.Unmarshal(yamlData, &gcfg)
	if err != nil {
		log.Fatalf("error parsing cellblock manifest: %v\n", err)
	}
	prog := prefix + "/bin/cblock"
	clist, err := ProcessManifest(gcfg, prog)
	if err != nil {
		log.Fatalf("failed to process manifest: %s\n", err)
	}
	failed := 0
	for _, cmd := range clist {
		line := strings.Join(cmd.Args, " ")
		if dryRun {
			fmt.Printf("%s\n", line)
			continue
		}
		c := exec.Command(cmd.Args[0], cmd.Args[1:]...)
		c.Stdout = os.Stdout
		c.Stderr = os.Stderr
		if err := c.Run(); err != nil {
			log.Printf("launch failed: %s: %v\n", line, err)
			failed++
		}
	}
	return failed
}

func main() {
	cfg := CmdArgs{}
	cfg.Prefix = pflag.StringP("prefix", "P", "/usr/local", "installation path")
	cfg.ManifestPath = pflag.StringP("manifest-path", "p", "",
		"path to cellblock manifest (default PREFIX/etc/cellblocks.yaml)")
	cfg.DryRun = pflag.BoolP("dry-run", "n", false,
		"print the cblock commands instead of running them")

	pflag.Parse()
	// The default depends on --prefix, so set it after parsing.
	if *cfg.ManifestPath == "" {
		*cfg.ManifestPath = *cfg.Prefix + "/etc/cellblocks.yaml"
	}
	cf, err := os.ReadFile(*cfg.ManifestPath)
	if err != nil {
		log.Fatalf("error reading YAML file: %v", err)
	}
	if failed := LaunchCellblocks(cf, *cfg.Prefix, *cfg.DryRun); failed > 0 {
		log.Fatalf("%d cellblock(s) failed to launch", failed)
	}
}
