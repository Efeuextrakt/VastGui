# Features

[← Back to the README](../README.md)

![Search page](screenshots/search.png)

## Search (main page)

- #GPUs toggles, rental type (on-demand / interruptible / reserved), multi-select GPU model picker with search
  (the vast.ai console's model list from `/api/v0/gpu_types/`, grouped by family - RTX 5000/4000/3000/2000 Series,
  GTX, Titan, RTX PRO / Ada / A, Quadro, Data Center, AMD Instinct / Radeon, Other - and sorted by model and variant),
  region (planet / continent / country), sort (price, TFLOPS/$/hr, DLP/$/hr, reliability, inet, disk bandwidth,
  duration, VRAM, CUDA) with asc/desc, card view or a sortable, resizable, reorderable, hideable table.
- Filter panel with template box (change/edit template), secure cloud, disk size + volume, presets, reliability,
  duration, specific machines / hosts, machine options, CUDA/driver/Ubuntu minimums, 20 min/max range sliders.
- **Presets** (under *Disk Space*): pick a saved filter set in the dropdown (trash icon deletes it), then *Save*
  (current filters, as a new name or overwriting the selected one), *Load* (apply the selected preset) or *Reset*
  (all filters back to the defaults).
- The *Search* button leads the results line, so it stays in the same place whatever the window size.
- **Machines / Hosts**: machine and/or host IDs (separated by spaces or commas) limit the search to exactly those
  machines (`machine_id` / `host_id` in the query sent to vast.ai, so they are found even outside the random
  sample). The other filters still apply.
- **Prices** match the vast.ai web console: vast.ai's discounted total (`search.discountedTotalPerHour`: GPU +
  storage for the requested disk size, plus an SLA premium if any). The disk size is the larger of the *Disk Space*
  filter and the template's recommended disk (the filter panel says so when the template needs more). For
  interruptible offers the price is the console's suggested bid (minimum bid + 5 % of the GPU price, also the
  default bid in the rent dialog); with *Create new local volume* the volume's cost is included. Bandwidth is
  billed separately. Hover a price for the breakdown.
- **Metrics**: `TFLOPS/$/hr = total_flops / price`, `DLP/$/hr = dlperf / price` (higher is better),
  colour-coded green -> red by percentile within the current result set; missing values show `—`.
- **Defective machine filter** (on by default, thresholds in Settings): reliability below X %
  (default 90 %), missing/zero `inet_down`, `inet_up`, `total_flops`, `dlperf`, `disk_bw`, `cuda_max_good`,
  DLPerf or TFLOPS per GPU more than 40 % below the median of the same GPU model in the current results
  (needs >= 3 samples of that model). A deverified status alone is *not* a defect - you decide by blacklisting
  machines.
  The results header shows how many defective offers are hidden; tick *Defective Machines* in *Machine Options*
  to list them with a red *defective* tag and border.
- **Blacklisted machines** are not "defective": blacklist a machine with the Ban button on the Instances page or an
  offer's right-click menu (manage the list in Settings > Blacklist). Their offers are hidden; tick *Blacklisted
  Machines* in *Machine Options* to list them with a grey *blacklisted* tag and border.
  Deverified offers are only listed when *Unverified Machines* and its sub-option *Deverified Machines* are ticked.
  Verification tags: verified green, unverified yellow, deverified orange.
- **Incompatible machines**: offers that fail the selected template's requirements (its `extra_filters`, e.g.
  `gpu_ram >= 40000`, plus the CUDA version in the image tag such as `...-cuda12.4`) get a red *INCOMPATIBLE*
  tag (card) or a red GPU name (table); the tooltip lists the unmet requirements. With *Incompatible Machines*
  unticked they are hidden. A template without requirements makes no offer incompatible (see the `?` help).
- **Machines rented by me**: offers on machines where you already have an instance (running, loading or stopped)
  are hidden; tick *Machines Rented by Me* to show them with a blue border and a disabled *RENTED* button (a blue
  row and *RENTED* in the table).
- **Template picker** (Search -> *Change Template*): tabs *My templates* (first; yours, public and private),
  *Recommended* and *Search*; select a row and press *Use selected*.
- **Rent dialog**: disk, label, bid price for interruptible, link/create volume and a one-line template summary
  with *Change...* (pick another template for this rental only; a warning shows if the machine does not meet its
  requirements); the template configuration (image/template hash, SSH/Jupyter/entrypoint + direct, env vars,
  ports, docker options, on-start script) is folded into a collapsed *Template settings* section.

## Search completeness

vast.ai does not return the true top offers for a search. Measured against the offers endpoint (Nov 2026):

- Without an API key every request is capped at **64 offers**, whatever `--limit` says. With a key the limit is
  honoured up to the full result size; `--limit 0` or no limit still gives 64.
- Each response is a **random sample** of the matching offers, correctly sorted. Small limits are not the best L
  offers: with limit 128 sorted by price only ~20% of the truly cheapest 10 offers were included.
- Multi-GPU machines are offered as 1/2/4/... GPU bundles, and every request picks other GPUs, so the **offer id
  changes**. An offer is identified by *machine + GPU count* instead.

VastGui therefore sends **5 identical requests with limit 1000** per search (Settings > Connection: *Offers per
request*; *Requests per search*, any value >= 1, recommended 3-10) and merges them, keeping one offer per machine +
GPU count (the cheapest).
Measured: 3 merged requests find the true best 10 offers and ~87% of the best 200, 4 find ~92% (a single
limit-128 request: ~20% / ~17%).
Ties are ordered by machine id, so equal values do not swap between searches. Results still vary slightly from
search to search because the server keeps sampling. Each request at limit 1000 downloads ~2.7 MB.

## Instances

- **Card view** (default; card / table toggle on the toolbar, default in Settings > Appearance): one wide card per
  instance, like the Search offer cards - status, name, GPU, machine / host and location; image, template, uptime,
  SSH command (copy) and ports; GPU / CPU / VRAM / disk usage bars; current cost (breakdown on hover), reserve
  badge, TFLOPS/$/hr and DLP/$/hr, $/hr trend and the action icons. Tick cards (or *Select all*) for the toolbar's
  bulk actions; selected cards get a blue border, outbid instances a red one. Right-click a card for the full
  menu, double-click for the raw JSON. Sort by name, ID, status, GPU, $/hr or uptime, ascending or descending
  (default name A-Z; instances without a name go last). The filter and status filter apply to both views.
- Status-coloured table (interruptible instances that should run but don't are shown as a red *outbid* badge
  that opens *Change bid*; also a status filter and a count in the header), bulk start/stop/reboot/destroy, label.
- **Real costs**: the *$/hr* column, the header total and the $/hr sparkline show what each instance costs right
  now, calculated like the vast.ai console from vast.ai's `instance` price: the normal price while running (a
  running bid +5 % on the GPU), only storage while stopped, and the reserved (discounted) price only while
  prepaid credit is left on the instance (`credit_balance > 0` and `credit_discount > 0`). The tooltip breaks it
  down (GPU / storage / reserved discount) and shows the normal running price. TFLOPS/$/hr and DLP/$/hr use the
  normal price.
- **Hourly total** in the header: what is billed to the account now. Prepaid (reserved) instances are paid from
  their own prepaid credit and are listed separately ("+ N prepaid: $x/hr").
- **Machine details**: hover the instance ID or the GPU name (table and cards), or the machine line (cards), for the
  machine's GPU,
  performance, CPU and cores, RAM, board, PCIe, disk, network, CUDA / driver / Ubuntu, reliability, location and
  verification.
- Logs window: separate Instance / Daemon log tabs, line numbers, compact line spacing, auto-refresh, save to file;
  tail defaults to 1000 lines, -/+ step 1000, *Max* = 20000.
- Copy SSH command / SCP URL, open Jupyter, change bid, prepay, deposit, execute, copy files (between remote
  locations), cloud copy, snapshot, attach SSH key, create template from instance, recycle, update, blacklist the
  machine.
- GPU/CPU/VRAM/disk usage bars, TFLOPS/$/hr and DLP/$/hr per instance and an ImPlot $/hr sparkline per instance.
- **Reserved discount** (*Reserve* column / row menu *Add a Reserved Discount...*): a *Save NN%* badge shows the
  best discount the host offers (*Saved NN%* once reserved). It opens a dialog like the web console with
  1 / 3 / 6 month plans (price, discount, discounted $/hr; plans ending after the machine's offer end date are left
  out) or a custom amount, and needs a second confirmation because the credits are deducted immediately. Discount
  per prepaid month count t: `credit_discount_max * t / (t + 1)` (matches the server; 30-day months).

## Templates

- Tabs *My templates*, *Recommended* and *Search* (query syntax, e.g. `name=PyTorch`; the query field is only on
  the Search tab).
- **Card view** like the vast.ai console (default; card / table toggle on the toolbar, default in Settings >
  Appearance): a responsive grid with name, image:tag, tags (recommended, public/private, launch mode, recommended
  disk, usage count), a two-line description (full text on hover) and *Select* (use for search / rent; the current
  template shows *SELECTED* with a blue border), *Edit*, *Duplicate* and a menu (copy hash / image, delete).
  Double-click a card to edit it; right-click for the same menu. Sort the cards by name or creation date,
  ascending or descending (date sorts newest first by default; templates without a date go last).
- Create / edit form: image, tag, login, launch mode, env vars, ports, docker options, search params, on-start
  script, readme.

## Billing

- Current credit and balance, and when the credit runs out at the current instance costs: time left and the local
  date and time (red under 1 day, orange under 3 days). Prepaid instances count from when their prepaid credit is
  used up, at their normal price. Bandwidth, new rentals and price changes are not included.
- Invoices and charges for a date range, chart and CSV export.

## Other pages

**Storage, Serverless, Earnings, Members/Team, Audit Logs, Keys, Hosting, Activity log,
Settings** - see the in-app pages. Every action logs the API request it sends (`> PUT /api/v0/...`, never the
request body) in the Activity log.

## User interface

- Nothing blocks the UI thread: all calls run on a `std::jthread` worker pool; results are delivered on the UI
  thread each frame. A running request spins the icon *inside* its own refresh/search button (fixed size, no
  layout shift); errors appear inline, as toasts and in the Activity log. The docked Activity window is toggled
  from the sidebar.
- Dialogs all have a close button (x) and close when you click anywhere outside them (the one-time *New API key*
  dialog only via x). Floating Logs / JSON windows also close on an outside click unless docked.
- Window size, position and docking layout are restored after a restart. Dark/light theme, accent colour,
  font size / UI scale, per-monitor DPI.
