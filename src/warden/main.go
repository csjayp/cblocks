package main

import (
	"fmt"
	"log"
	"os"

	"gopkg.in/yaml.v3"
)

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

type Config struct {
	Cellblocks []Cellblock `yaml:"cellblocks"`
}

func processPorts(portMappings []PortMapping) {
	for _, port := range portMappings {
		if port.Public {
			fmt.Printf("--port %s:%s:public\n",
			    port.HostPort, port.ContainerPort)
		} else {
			fmt.Printf("--port %s:%s\n",
			    port.HostPort,
			    port.ContainerPort)
		}
	}
}

func processVolumes(vols []Volume) {
	for _, vol := range vols {
		fmt.Printf("--volume %s:%s:%s:%s\n",
			vol.FsType, vol.Origin,
			vol.MountPoint, vol.Perms)
	}
}

func processManifest(gcfg Config) {
	for _, cb := range gcfg.Cellblocks {
		fmt.Printf("Launching image: %s\n", cb.Image)
		if len(cb.Volumes) > 0 {
			processVolumes(cb.Volumes)
		}
		if len(cb.Ports) > 0 {
			processPorts(cb.Ports)
		}
	}
}

func main() {
	var gcfg Config

	cf, err := os.ReadFile("config.yaml")
	if err != nil {
		log.Fatalf("error reading YAML file: %v", err)
	}
	err = yaml.Unmarshal(cf, &gcfg)
	if err != nil {
		log.Fatalf("error parsing cellblock manifest: %v\n", err)
	}
	processManifest(gcfg)
}
