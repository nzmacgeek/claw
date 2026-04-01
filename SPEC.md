# Specification Document for Build

## Concept
Claw is the PID 1 orchestrator, responsible for bringing the system from "just booted" to fully operational, reacting to events such as power, network, files and hardware and ensuring the system is in an appropriate state at those times.
### Services
Services are units of work: daemons, one‑shot tasks, background workers, or system components that must be started, stopped, or supervised.
A service is a definition of:
- what to run
- how to run it
- when to run it
- what it depends on
- how to restart or supervise it
### Service Types:
- simple (foreground tasks)
- forking (traditional daemons that go to the background)
- oneshot (complete a command)
- notify (use IPC to signal service readiness - pipes, sockets, etc)
- timer (fired by a timer)

### Service Attributes
- Name
- Description
- Start command, arguments, working directory
- Stop command, arguments, working directory
- Reload command, arguments, working directory
- User and group to execute as
- Environment variables to pass to the service
- Dependencies (required, optional, order)

Claw must parse these definitions, resolve their dependencies and start/stop them in the right order, track their states and supervise them.
Restart them if required. Capture their logs (stdout/stderr) to a log file in /var/log.
Claw must provide easy ways to enable administrators to manage and monitor services.

## Events
Events are triggers that cause services to start, stop, reload, or transition the system.

An event is something that happens:
- system lifecycle transitions
- timers
- hardware changes
- file system changes
- network availability
- user logins
- custom signals

 Types of events
You’ll likely want:
Lifecycle events (boot, shutdown, reboot, single-user)
Timers
Hardware (device ready, inserted, removed)
Filesystem (creation/modification of files or directories)
Network (interface up, IP address acquired)


✔ Required configuration fields
An event definition might include:
- Name
- Type (timer, lifecycle, hardware, etc.)
- Match conditions
- e.g., device path, network interface, cron‑like schedule
- Actions
- start service
- stop service
- reload service
- activate target
- Debounce / rate limiting (avoid storms)
- Persistence (should the event fire if it happened before Claw started?)
✔ What Claw must do with events
- Monitor kernel interfaces (udev, netlink, inotify, timers)
- Maintain an event dispatcher
- Match events to services/targets
- Ensure events respect dependency ordering
- Avoid loops or thrashing
- Provide introspection (clawctl events)

Targets
Targets represent system states—collections of services that must be active to consider the system “in that state.”
Think of them as named milestones.
✔ What a target is
A target is:
- a grouping of services
- a synchronization point
- a desired system state
✔ Examples
- boot.target — minimal system up
- multiuser.target — networking + login services
- graphical.target — GUI stack
- shutdown.target — orderly teardown
- rescue.target — minimal recovery environment
✔ Required configuration fields
A target definition might include:
- Name
- Description
- Requires= (services/targets that must be active)
- Wants= (optional dependencies)
- Before=/After= (ordering)
- Conflicts= (e.g., graphical.target conflicts with shutdown.target)
- Default? (is this the default boot target?)
✔ What Claw must do with targets
- Resolve dependency trees
- Activate all required services
- Track when a target is “reached”
- Allow switching targets (e.g., boot → multiuser → graphical)
- Handle conflicts cleanly
- Provide introspection (clawctl isolate rescue.target)


## Configuration
Configuration stored in /etc/claw/
Main claw configuration is /etc/claw.conf, stored in yaml format.
Services stored in yaml format in /etc/claw/services.d
Service Targets stored in yaml format in /etc/claw/targets.d

## Architecture
To make Claw a real, modern init system, we need to think about:

 ### Dependency Resolution
- Directed acyclic graph (DAG) of units
- Cycle detection
- Ordering constraints
- Parallel startup where possible
 ### Process Supervision
- PID tracking
- Restart policies
- Exit code handling
- Logging and stdout/stderr capture
- Optional watchdog timers

### Resource Management
If we want to be modern:
- cgroup v2 integration
- CPU/memory/IO limits
- Service accounting (how much CPU did sshd use?)

### Security
- Drop privileges
- Namespaces (optional)
- Seccomp filters
- Capability bounding sets
- Read‑only rootfs support
- Service sandboxing

### Event Monitoring Backends
BlueyOS will need:
- netlink‑like interface for hardware
- inotify‑like FS events
- timerfd‑like timers
- signals
- your own event bus

### State Machine
Claw must maintain:
- system state (booting, running, shutting down)
- service states
- target states
- event queue
- dependency graph

### User Tools
We will want a companion CLI:
- clawctl start <service>
- clawctl stop <service>
- clawctl status <service>
- clawctl list-units
- clawctl isolate <target>
- clawctl reload-daemon

### Typical approach from claw startup
- Kernel loads Claw as PID 1
- Claw loads all unit definitions (services, events, targets)
- Builds dependency graph
- Activates default target
- Starts required services in parallel where possible
- Monitors events and reacts dynamically
- Supervises services for the lifetime of the system
- Handles shutdown cleanly

### Example visualization
Below is a complete, functional boot sequence, with the *vibe* of The Magic Claw woven through the naming, logging, and transitions.

---

# 🐾 **Claw Boot Sequence — “The Magic Claw Chooses Who Will Go and Who Will Stay”**

This sequence is structured like a real init system state machine, but with a Bluey‑style personality layered into the logs, targets, and activation flow.

---

# **Stage 0 — Kernel Hands Over to Claw (PID 1)**  
**System State:** `claw:init`  
**Goal:** Establish the minimal environment Claw needs to run.

### Actions
- Mount `/proc`, `/sys`, `/dev`
- Load essential kernel modules
- Initialise Claw’s internal event loop
- Parse unit files from:
  - `/etc/claw/units`
  - `/usr/lib/claw/units`

### Fun flavour  
Claw prints a banner:

```
[claw] The Magic Claw awakens…
[claw] Choosing who will go first…
```

---

# **Stage 1 — Early Boot Target (`claw-early.target`)**  
**Purpose:** Bring the system to a point where real services can start.

### Services activated
- `claw-filesystems.service`  
- `claw-random-seed.service`  
- `claw-tmpfs.service`  
- `claw-udev-init.service` (or your equivalent)

### Events monitored
- early hardware enumeration  
- block device availability  

### Fun flavour  
When a device appears:

```
[claw] A wild device appears! The Magic Claw approves.
```

---

# **Stage 2 — Device & Hardware Bring‑Up (`claw-devices.target`)**  
**Purpose:** Ensure hardware is ready before higher‑level services.

### Services
- `claw-udev-trigger.service`
- `claw-udev-settle.service`
- `claw-kmod.service`

### Events
- netlink/udev events  
- block device readiness  
- root filesystem availability  

### Fun flavour  
When waiting for hardware:

```
[claw] The Magic Claw is pondering… waiting for the right moment…
```

When hardware is ready:

```
[claw] The Magic Claw has chosen! Proceed.
```

---

# **Stage 3 — Root Filesystem & System Integrity (`claw-rootfs.target`)**  
**Purpose:** Mount rootfs read‑write, check integrity, prepare system dirs.

### Services
- `claw-remount-root.service`
- `claw-fsck-root.service`
- `claw-swap.service`

### Fun flavour  
On successful mount:

```
[claw] The Magic Claw declares the root strong and worthy.
```

---

# **Stage 4 — Basic System Bring‑Up (`claw-basic.target`)**  
**Purpose:** Start the foundational services that everything else depends on.

### Services
- logging daemon  
- Claw’s own IPC bus  
- hostname setup  
- local‑filesystem mounts  
- time sync (or stub)  
- entropy seeding  

### Fun flavour  
```
[claw] The Magic Claw is assembling the essentials…
```

---

# **Stage 5 — Networking (`claw-network.target`)**  
**Purpose:** Bring up network interfaces, DHCP, routing, DNS.

### Services
- `claw-netlink.service`
- `claw-networkd.service`
- `claw-dhcp.service`
- `claw-resolved.service`

### Events
- interface up/down  
- DHCP lease acquired  
- link state changes  

### Fun flavour  
When network comes online:

```
[claw] The Magic Claw reaches out… and the world answers!
```

---

# **Stage 6 — Multi‑User System (`claw-multiuser.target`)**  
**Purpose:** System is now usable in console mode.

### Services
- login manager (getty)
- SSH daemon
- cron/timers
- system message bus
- user session manager

### Fun flavour  
```
[claw] The Magic Claw welcomes all challengers. Log in if you dare!
```

---

# **Stage 7 — Graphical System (`claw-graphical.target`)**  

### Services
- display server (Wayland/X11)
- compositor
- desktop environment
- graphical login manager

### Fun flavour  
```
[claw] The Magic Claw reveals the shiny bits!
```

---

# **Stage 8 — Idle / Runtime Operation (`claw-runtime.target`)**  
**Purpose:** System is fully operational. Claw now shifts to event‑driven mode.

### Responsibilities
- supervise services  
- respond to events  
- handle timers  
- manage shutdown/reboot requests  

### Fun flavour  
```
[claw] The Magic Claw rests… but watches everything.
```

---

# **Shutdown Sequence (`claw-shutdown.target`)**  
**Purpose:** Cleanly stop services, unmount filesystems, and power off.

### Steps
- stop user sessions  
- stop network  
- stop daemons  
- sync disks  
- unmount filesystems  
- poweroff/reboot  

### Fun flavour  
```
[claw] The Magic Claw has spoken. Everyone must go now.
```

---

**Putting It All Together — Boot Flow Summary**

```
claw:init
  → claw-early.target
  → claw-devices.target
  → claw-rootfs.target
  → claw-basic.target
  → claw-network.target
  → claw-multiuser.target
  → claw-graphical.target (optional)
  → claw-runtime.target
```

Each target is a synchronization point.  
Each service declares dependencies.  
Events can activate services or targets at any time.


