package main

import (
	_ "embed"
	"reflect"
	"testing"

	"gopkg.in/yaml.v3"
)

// Load our YAML data in from the various test fixtures

//go:embed fixtures/launchImageNoParams.yaml
var yamlLaunchImageNoParams []byte

//go:embed fixtures/launchNoImage.yaml
var yamlLaunchNoImage []byte

//go:embed fixtures/launchWithVolumes.yaml
var yamlLaunchWithVolumes []byte

//go:embed fixtures/launchWithPseudoFs.yaml
var yamlLaunchWIthPseudoFs []byte

//go:embed fixtures/launchWithPortMappings.yaml
var yamlLaunchWithPortMappings []byte

//go:embed fixtures/launchWithFalsePseudoFs.yaml
var yamlWithFalsePseudoFs []byte

//go:embed fixtures/launchWithMultiBlocks.yaml
var yamlLaunchWithMultiBlocks []byte

func TestLaunchCellblocks(t *testing.T) {
	prog := "/usr/local/bin/cblock"
	testCases := []struct {
		name         string
		yamlData     []byte
		expectCmdVec []CmdVec
		wantError    bool
	}{
		{
			name:     "launch multiple cellblocks",
			yamlData: yamlLaunchWithMultiBlocks,
			expectCmdVec: []CmdVec{
				{
					Args: []string{
						"/usr/local/bin/cblock",
						"launch",
						"--no-attach",
						"--name base:14.3",
						"--network vlan0",
						"--fdescfs",
						"--volume ufs:/dev/zvol/pool0/db-storage:/data:rw",
						"--volume zfs:pool0/dataset:/zfs/dataset:rw",
					},
				},
				{
					Args: []string{
						"/usr/local/bin/cblock",
						"launch",
						"--no-attach",
						"--name nginx",
						"--network vlan0",
						"--volume ufs:/storage/vol3/www-content:/www:rw",
					},
				},
			},
			wantError: false,
		},
		{
			name:         "empty config triggers error",
			yamlData:     []byte(""),
			expectCmdVec: []CmdVec{},
			wantError:    true,
		},
		{
			name:     "launch image with PseudoFs explicitly disableds",
			yamlData: yamlWithFalsePseudoFs,
			expectCmdVec: []CmdVec{
				{
					Args: []string{
						"/usr/local/bin/cblock",
						"launch",
						"--no-attach",
						"--name base:15-STABLE",
						"--network vlan0",
					},
				},
			},
			wantError: false,
		},
		{
			name:     "launch image with Port Mappings",
			yamlData: yamlLaunchWithPortMappings,
			expectCmdVec: []CmdVec{
				{
					Args: []string{
						"/usr/local/bin/cblock",
						"launch",
						"--no-attach",
						"--name base:14.3",
						"--network vlan0",
						"--port 443:443:public",
						"--port 80:80",
					},
				},
			},
			wantError: false,
		},
		{
			name:     "launch image with Pseudo Fs",
			yamlData: yamlLaunchWIthPseudoFs,
			expectCmdVec: []CmdVec{
				{
					Args: []string{
						"/usr/local/bin/cblock",
						"launch",
						"--no-attach",
						"--name base:14.3",
						"--network vlan0",
						"--fdescfs",
						"--procfs",
						"--tmpfs",
					},
				},
			},
			wantError: false,
		},
		{
			name:     "launch image no paramters",
			yamlData: yamlLaunchImageNoParams,
			expectCmdVec: []CmdVec{
				{
					Args: []string{
						"/usr/local/bin/cblock",
						"launch",
						"--no-attach",
						"--name base:14.3",
						"--network vlan0",
					},
				},
			},
			wantError: false,
		},
		{
			name:         "cellblock with no image specification",
			yamlData:     yamlLaunchNoImage,
			expectCmdVec: []CmdVec{},
			wantError:    true,
		},
		{
			name:     "cellblock with volume specifications",
			yamlData: yamlLaunchWithVolumes,
			expectCmdVec: []CmdVec{
				{
					Args: []string{
						"/usr/local/bin/cblock",
						"launch",
						"--no-attach",
						"--name base:14.3",
						"--network vlan0",
						"--fdescfs",
						"--volume ufs:/dev/zvol/pool0/db-storage:/data:rw",
						"--volume zfs:pool0/dataset:/zfs/dataset:rw",
					},
				},
			},
			wantError: false,
		},
	}
	for _, tt := range testCases {
		var gcfg Config
		err := yaml.Unmarshal(tt.yamlData, &gcfg)
		if err != nil {
			t.Fatalf("failed to unmarshal yaml: %s\n", err)
		}
		ret, err := ProcessManifest(gcfg, prog)
		if tt.wantError && err == nil {
			t.Fatalf("%s: should have failed but didnt\n", tt.name)
		}
		if err != nil && !tt.wantError {
			t.Fatalf("ProcessManifest '%s' failed: %s\n", tt.name, err)
		}
		if err != nil && tt.wantError {
			continue
		}
		equal := reflect.DeepEqual(ret, tt.expectCmdVec)
		if !equal {
			t.Fatalf("'%s' expected:\n'%v' got:\n'%v'\n", tt.name, tt.expectCmdVec, ret)
		}
	}
}
