# Personas

Vision statement: "Improve the uptime of the accelerator by enabling everyone to gain the insights to spot and fix problems ASAP"
plus the broader community goal around free software for signal processing.

Personas are fictional yet data-driven archetypes representing key user segments.
They're not just made-up characters;
they are generally built from research-interviews and workshops.
Defining personas helps teams to prioritize features and avoid designing in a vacuum.
In projects like OpenDigitizer,
personas bridge the gap between technical complexity and human usability.
They ensure the UI isn't just functional but usable by different types of users.

For FAIR there are four basic user groups:

1. Machine experts (12 ppl per accelerator)
   They look from top and look at the lateral view, what system is responsible for the problem and refer to system experts ("your magnet is the problem")

2. System experts (similar to ^) (~100 ppa)
   They are responsible for the vertical stack (power converter stack; dealing with magnets; ...)

3. Operators (25 ppa)
   They monitor, see machine is running, and report issues at more shallow level, do pre-diagnostics. Available 24/7. They are technicians, not physics experts

4. Others - experimental users 1000s ppa
   This user group contains the public (as data will be publically available), management and scientists having their experiment run on the accelerator

The main difference in work overall is the degree to which users do

  - Monitoring or
  - Bug-tracing

We decided to create two personas on both edges of this spectrum:

- Susan -- She is very experienced in accelerator relevant physics and knows basic programming. Her main task is to trace bugs to keep the accelerator up and running.
- Paul -- He is an early graduate (bsc) and does not have programming experience. He works as an operator and his job is to monitor the accelerator working correctly.
          He does first level bug fixing. In case he cannot fix himself, he has to identify the right person to escalate to and provide a report of the problem.

The UX for both personas should
mirror their workflows and
ease usage in the cases that are of high priority to them.

## Susan

Susan is a seasoned accelerator physicist
or system specialist,
and has a solid grasp on programming basics.
She's the one triaging bugs across the accelerator's vertical stacks.

Susan's priorities are
robust signal validation,
crafting custom dashboards,
and
defining signal processing flowgraphs.

### Workflow pattern:

Opens the OpenDigitizer Application either via

a) (secondary case) direct link/program argument to open a specific Dashboard,
b) (primary case) chooses a predefined Dashboard from a 'favourite'/curated list (e.g. tree-view of all, last used, ...)

She uses both the 'Chart UI' and the 'Flow-graph UI' and
flips back and forth between them.


### Main tasks (rated by importance from 0-5, 0 - not important, 5 - critical):

- (5) I want to see whether a signal is correct:
Compare signals to expectations, to other signals, check min/max thresholds;
  - UI operations: zoom/pan charts, define thresholds, ...

- (4-) I want to define signal transformations:
  - UI operations: browse existing signals, transformation blocks,
    create flowgraphs, change block parameters at runtime,
    change acquisition mode of a signal in the flowgraph, ...

- (3) I want to create reusable transformation chains:
  - UI operations: promote and demote a group of selected blocks to a sub-graph,
    save sub-graph definition and instantiate a sub-graph from a previously saved definition
    (with both reference and value semantics),
    editing sub-graphs, ...

- (3) I need to be able to create dashboards:
  - UI operations: Save dashboards with names/tags and screenshots,
    browse and load previously created dashboards...

- (4+) I want to access a set of pre-defined signals:
  Related to "Define signal transformations", she needs to be able
  to browse and filter through the available signals.
  - UI operations: Browse a list of signals to add as a block
  to the flowgraph, or as a new chart to the chart page

- (3) I want to be automatically notified about abnormal states:
  - UI operations: Define thresholds/envelopes; trigger notifications,
  snapshots (pre/post), or external alerts for post-mortem analysis.

- (5) I want to be able to change how charts are shown
  - UI operations: Move charts around, merge charts,
    change chart layout



## Paul

Paul is an entry-level operator,
with no coding experience.
He needs to monitor for anomalies in real-time.
Paul's requirements are straightforward:
instant access to pre-built dashboards for monitoring for anomalies;
simple alerts via thresholds with basic snapshots;
and easy overviews of signals or errors.

He needs a UI that's minimalist by default.
This reflects accessibility heuristics in UX:
progressive disclosure (hide the advanced stuff)
ensures he doesn't hit walls, while still allowing growth if he levels up.

### Workflow pattern:

Opens the OpenDigitizer Application either via

a) (primary case) direct link/program argument to open a specific Dashboard, or
b) (secondary case) chooses a predefined Dashboard from a 'favourite'/curated list (e.g. tree-view of all, last used, ...)

He enters and would interact directly with the 'Chart UI' view
and rarely with the 'Flow-Graph UI'
(adding new data sources and charts would be done in the 'Chart UI' view).


### Main tasks (rated by importance from 0-5, 0 - not important, 5 - critical):

- (5) I want to see whether a signal is correct:
  (Same as Susan)

- (5) I want to view a set of pre-defined signals:
  Not same as Susan's.
  UI operations: Select a signal to add it to the charts page directly.

- (4) I want to apply a transformation to a selected signal:
  I don't want a flowgraph editor, I just want to be able to apply
  some simple transformation like FFT to a selected signal

- (3) I want to be automatically notified about abnormal states
  (Same as Susan)

- (5) I want to be able to change how charts are shown
  (Same as Susan)



By framing the UI around these personas,
we're designing the UX
in a way that it is trainable for Paul,
while extensible for Susan.
It's a classic empathetic design move:
one interface, layered interactions, tested through prototypes.



*Note:*

This document does not define a persona representing people who deploy the system,
as that is not a persona that *uses* OpenDigitizer.
But it is still an aspect of the whole system
that needs to be kept in mind.



## More detailed UI operations for aforementioned user stories:

A few of the above include the subtasks:

   - I want to get an overview of available signals
   - I want to easily find a (set of) signal(s) I need

I want to see whether a signal is correct (Susan and Paul):

   - compared to expected (in mental model)
   - compared to a second signal (same time or frequency axes)
   - correlated with a second signal (value 1 versus value 2)
   - compared to a predefined tolerance band around a reference
   - compared to a user-defined min max thresholds
   - is signal even shown
   - see error log for the signal
   - usual charting features
   - usual post processing step (task below)
   - saving the snapshots of all the shown data sets,
     maybe even with the reloadable dashboards with the old data
     screenshots

I want to create a dashboard -- reusable view of signals (Susan):

   - save/store - define name/tags, saves screenshot
   - group signals into the same chart (shared or separated axes)

I want to view a dashboard -- pre-defined set of signals + layout (Susan and Paul):

   - finding the right dashboard
   - open selected dashboard
   - from outside: launchable via URL

I want to create a signal transformation based on basic transformation blocks (Susan):

   - @see GNU Radio companion
   - frontend and UI flowgraphs
   - editable flowgraphs - topology and parameters
     (parameters editable during runtime of the flow, not of the service)
   - dataset processing
   - load and store

I want automatically (trigger) to be notified about non-normal states (Susan and Paul):

   - define thresholds (min max)
   - trigger envelopes
   - impl-wise: specialized post-processing
   - actions:
       - notification on the machine
       - external notification
       - saving snapshots - pre, actual and post signals - for a defined set of signals
         (transient recording) - aka post mortem analysis

I want to be able to change how charts are shown (Susan and Paul)

   - change chart layout
   - move charts
   - add new charts from selected signal (@see 'I want to view a set of pre-defined signals')
   - remove existing charts


# UI hierarchy:

The main focus for the workspace is:

   - Finding the right dashboards
   - Managing a set of dashboards
   - Creating new dashboards

The main focus of the dashboard is:

   - Layouting the charts
   - Manipulating charts (e.g. zoom, disable and enable sources, colors,...)
   - Arranging charts (e.g. choose layout, arrange charts)
   - Admin (e.g. save, share, print, ...)
   - Adding simple signals

The flowgraph allows the users to define the content (data sinks) of the charts shown on the dashboard.
The main focus of the flowgraph is:

   - Manage signals (add, remove, disable, ...)
   - Allow to visually modify / transform the signals (by connecting available blocks)
   - Define sinks to be used in the charts
