# NOTES

## Overview

Cblocks is a lightweight container build and runtime environment built on **FreeBSD jails**, designed for simplicity, performance, and deep integration with FreeBSD’s native subsystems.  
The system is implemented in **C**, **Shell**, and **Go**, combining low-level efficiency with high-level orchestration and build logic.

---

## Architecture

- **Base Runtime:** Utilizes FreeBSD jails as the core container isolation mechanism.  
- **Storage Backends:** Supports both **UFS/unionfs** and **ZFS** for flexibility and performance.  
- **Persistent Console:** Each container is assigned its own **PTY**, providing a persistent, reattachable console similar to `screen` or `tmux`.  
- **Networking:** Leverages FreeBSD’s native networking stack, supporting both **bridge** and **NAT** modes via the **PF firewall**.  
- **Auditing & Integrity:** Integrates with **setaudit** to apply OS-level auditing policies and uses **mtree-based snapshots** for file integrity monitoring (FIM).  
- **Tracing:** Exposes **cell block–specific DTrace providers** to assist with debugging and container lifecycle analysis.  

---

## Build System

- The **Cblockfile** is the build definition format used by Cblocks.  
  - Syntax is designed to be familiar to users of **Dockerfiles**, but adapted for FreeBSD.  
- The **forge image** provides the toolchain and environment required to process Cblockfiles.  
  - Acts as the **base (layer 0)** image needed before building any other cellblocks.  
- Builds produce self-contained container images that can be deployed or orchestrated via **Warden**.  

---

## Orchestration

- **Warden** handles orchestration and startup management.  
  - Defines containers to be launched automatically on system startup.  
  - Supports configuration of **port mappings**, **volumes**, and **networking modes**.  
- Runtime behavior and dependencies are described declaratively alongside container definitions.  

---

## Dependencies

The following tools are required on the host system for building:

- **C toolchain** (clang or gcc)  
- **Go toolchain** – required to build Go-based components  

The following tools are required by the cblocks runtime:

- **setaudit** – enables audit configuration pinning inside containers
- **subcalc** – provides subnet and IP calculation utilities for networking

All required tools are available through the **FreeBSD Ports Collection**.

---

## Future Work

- Achieve **OCI compliance** for interoperability with existing container ecosystems.  
- Expand **DTrace integration** for advanced observability and performance tuning.  
- Enhance **snapshot and rollback** support, leveraging **ZFS clones** and dataset features.  

---

## Developer Notes

- Ensure the **forge image** is built and present before attempting to build other cellblocks.  
- Test network configurations (bridge/NAT) carefully when using **PF**, especially if running multiple Cblocks instances.  
- When debugging build issues, attach directly to the **container PTY** for persistent console access.  
- DTrace providers can be used to trace container lifecycle events, performance metrics, and system call activity.  

