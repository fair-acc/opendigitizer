# Internal FAIR documentation: dashboards

Dashboards are files which describe the state of opendigitizer. They include
the state of the flowgraph as well as the way the results of analysis should be
displayed in charts. The charts display is usually called the "dashboard view"
or "dashboard page."

It is useful to share dashboards. Part of Susan's user story is that she needs
to be able to create and then share a dashboard file via a link to Paul.

Besides direct links, dashboards may also be acquired via "sources" in the
save/load dashboards page. Sources may be remote endpoints or locations on the
local filesystem. The primary way dashboards are shared at FAIR is a git
repository. This repository can then be cloned and updated locally, and the
source can point at the local clone of the repository. Different users may have
different permissions to the git repository, and dashboards submitted there
undergo review.

Due to the presence of many researchers and many dashboards, it is necessary
for dashboards to have tags, so there can be a search and filter UI to narrow
down searches. Dashboard authors must manually specify these tags. There
are both string tags and key/value tags. Most dashboards will have the
following keys:

- `"machine"`: this may be something like `SIS-100`
- `"device"`: something like `GS11MU2` (see the
  [signal identifier structure](#signal-identifier-structure) section)
- `"department"`: something like "RF Cavity"

There may additionally be other string tags which do not have a particular key.
For example, if a dashboard is not primarily to do with a certain device or
signal, but is in some way related, the identifier for that may be included in
the string tags.

# Signal identifier structure

Signals are often identified by a short string. This is a legacy format which
requires a bit of knowledge to understand which short acronyms correspond to
which machines/departments/locations. This only describes the general structure
of the identifiers:

> GS11MU2:
> GS -> SIS18
> 11 -> cell 11 (position in ring)
> M -> Magnet
> MU -> main dipole Magnet
> 2 -> Sub-converter/circuit/feedpoint

## FAIR org structure and implications for dashboard data

Fair is organized in the following hierarchy:

Division -> Department -> Group -> Team

Divisions are groups of 800-900 or so people. These include:

- Staff at the new SIS facility
- Staff at the old FAIR facility
- Experiments- visiting scientists

Departments are groups of as few as six or seven, up to eighty at the largest.
They are grouped based on the technologies they oversee. Some examples are RF
cavity, magnets, power converters, or vacuum systems.

Groups are subdivisions which are only relevant for the larger departments
(smaller departments only contain one group, which contains all the staff
there). Some groups include the industrial controls group and the timing
controls group. Teams are a further subdivision of groups and are usually not
relevant for dashboards.

Signals are organized by the following hierarchy:

Machine -> Types of devices -> Subtypes of devices -> Devices (Signals)

Machines are connected components on the FAIR campus which are used in a chain
to accelerate, store, observe, and collide particles. Examples are the SIS18
and SIS100 accelerators, and the ESR (experimental storage ring).

Device types correlate roughly to departments.

Device subtypes are things like big vs. small power converters.

Devices emit signals. An example of a dashboard which may often be associated
with a device is one which views all of (or at least the important) signals
from the device, aggregated. There may be thousands of signals, total.
