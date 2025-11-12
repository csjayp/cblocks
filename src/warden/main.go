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
	"strings"

	"github.com/spf13/pflag"
	"gopkg.in/yaml.v3"
)

type CmdArgs struct {
	ManifestPath *string
	Prefix       *string
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
	comp := "--" + option
	comp = comp + " " + value
	c.Args = append(c.Args, comp)
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

func LaunchCellblocks(yamlData []byte, prefix string) {
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
	for _, cmd := range clist {
		fmt.Printf("%s", strings.Join(cmd.Args, " "))
	}
}

func main() {
	cfg := CmdArgs{}
	cfg.Prefix = pflag.StringP("prefix", "P", "/usr/local", "installation path")
	manifestPath := *cfg.Prefix + "/etc/cellblocks.yaml"
	cfg.ManifestPath = pflag.StringP("manifest-path", "p", manifestPath, "path to cellblock manifest")

	pflag.Parse()
	cf, err := os.ReadFile(*cfg.ManifestPath)
	if err != nil {
		log.Fatalf("error reading YAML file: %v", err)
	}
	LaunchCellblocks(cf, *cfg.Prefix)
}
