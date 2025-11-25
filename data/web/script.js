const url = "http://gps.local";

function showSnackbar(msg, type = "default", duration = 3000) {
	let sb = document.getElementById("snackbar");
	if (!sb) {
		sb = document.createElement("div");
		sb.id = "snackbar";
		sb.className = "snackbar";
		document.body.appendChild(sb);
	}
	sb.textContent = msg;
	sb.className =
		"snackbar " +
		(type === "success" ? "success" : type === "error" ? "error" : "");
	requestAnimationFrame(() => sb.classList.add("show"));
	clearTimeout(sb._t);
	sb._t = setTimeout(() => sb.classList.remove("show"), duration);
}

function setButtonLoading(btn, loading, text) {
	if (!btn) return;
	if (loading) {
		if (!btn.dataset.o) btn.dataset.o = btn.innerHTML;
		btn.disabled = true;
		btn.innerHTML = `<span class="btn-spinner" aria-hidden="true"></span>${
			text || ""
		}`;
	} else {
		btn.disabled = false;
		if (btn.dataset.o) {
			btn.innerHTML = btn.dataset.o;
			delete btn.dataset.o;
		}
	}
}

const fmtTime = (t) => {
	const d = new Date(1000 * t);
	const p = (n) => String(n).padStart(2, "0");
	return `${p(d.getDate())}.${p(d.getMonth() + 1)}.${d.getFullYear()} ${p(
		d.getHours()
	)}:${p(d.getMinutes())}`;
};

const fmtHM = (s) =>
	`${String((s / 3600) | 0).padStart(2, "0")}:${String(
		((s % 3600) / 60) | 0
	).padStart(2, "0")}`;

const sortFunction = (a, b) => a.name.localeCompare(b.name);

async function loadFiles() {
	const res = await fetch(url + "/files");
	const d = await res.json();
	const tbody = document.querySelector("#fileTable tbody");
	tbody.innerHTML = "";

	if (!d.files || d.files.length === 0) {
		tbody.innerHTML =
			'<tr><td colspan="3" style="text-align:center;color:#777;">Keine Dateien gefunden</td></tr>';
		return;
	}

	const szp = d["sizePoint"];

	d.files.sort(sortFunction);
	d.files.forEach((file) => {
		const tr = document.createElement("tr");
		const t = fmtHM(file["len"] / szp);
		if (file["active"]) {
			tr.innerHTML = `<td>${file["name"]}</td><td>${t}</td><td></td>`;
		} else {
			tr.innerHTML = `
      <td>⬇️ <a class="download" href="${url}/download?file=/${file["name"]}">${file["name"]}</a></td>
      <td>${t}</td><td><button onclick="deleteFile('${file["name"]}')">🗑️</button></td>`;
		}
		tbody.appendChild(tr);
	});
}

async function loadFooter(d) {
	let data = d;
	if (!data) {
		const r = await fetch(url + "/info");
		data = await r.json();
	}
	if (!(data && data.totalBytes)) return;

	const f = document.querySelector("#footer");
	const freeKB = Math.floor((data.totalBytes - data.usedBytes) / 1024);
	const utc = data.time_t;
	const free = data.totalBytes - data.usedBytes;
	const ind = data.loggingActive
		? `<span id="recordIndicator" class="rec active" title="Aufzeichnung aktiv" aria-hidden="true"></span><span class="rec-label">Aufzeichnung aktiv</span>`
		: `<span id="recordIndicator" class="rec inactive" title="Nicht aktiv" aria-hidden="true"></span><span class="rec-label">Nicht aktiv</span>`;

	f.innerHTML = `${fmtTime(utc)} · ${ind} · ${freeKB}KB (${fmtHM(
		free / data.sizePoint
	)}) frei`;
	window.currentLoggingActive = !!data.loggingActive;

	const tb = document.getElementById("toggleLogBtn");
	if (tb) {
		if (window.currentLoggingActive) {
			tb.textContent = "⏹️ Stop Log";
			tb.classList.remove("danger");
		} else {
			tb.textContent = "⏺️ Sofort starten";
			tb.classList.add("danger");
		}
		// Toggle nur aktiv, wenn ein GPS-Fix vorhanden ist (gpsQuality > 0)
		if (typeof data.gpsQuality !== "undefined") {
			const hasFix = Number(data.gpsQuality) > 0;
			tb.disabled = !hasFix;
			if (!hasFix) {
				tb.textContent = "Kein Fix";
				tb.classList.add("disabled");
			} else {
				tb.title = "";
				tb.classList.remove("disabled");
			}
		}
	}
}

async function loadStatus() {
	const fixstr = [
		"-",
		"GPS",
		"DGPS",
		"PPS",
		"RTK",
		"FloatRTK",
		"Estimated",
		"Manual",
		"Simulated",
	];
	const r = await fetch(url + "/info");
	const d = await r.json();
	const sec = document.getElementById("statusInfo");
	if (!sec) return;
	const rows = [];
	const add = (k, v) => rows.push(`<tr><th>${k}</th><td>${v}</td></tr>`);
	add("Fix Qualität", fixstr[d.gpsQuality] || "—");
	add("RAM freie Gesamt", d.RAMtotalFree);
	add("RAM min frei", d.RAMminFree);
	add("RAM größter Block", d.RAMlargestFreeBlock);

	const w = document.getElementById("statusTableWrap");
	if (w) w.innerHTML = `<table class="status-table">${rows.join("\n")}</table>`;

	if (typeof d.logMode !== "undefined") {
		const v = String(d.logMode);
		const i = document.querySelector(
			`#logModeForm input[name=logmode][value='${v}']`
		);
		if (i) i.checked = true;
	}

	await loadFooter(d);
}

async function loadFilesAndFooter() {
	await loadFiles();
	await loadFooter();
}

async function deleteFile(file) {
	if (!confirm(`Datei "${file}" löschen?`)) return;
	await fetch(url + `/delete?file=/${file}`);
	if (document.querySelector("#fileTable")) await loadFilesAndFooter();
	if (document.querySelector("#statusInfo")) await loadStatus();
}

async function deleteAll() {
	if (!confirm("Wirklich ALLE Dateien löschen?")) return;
	await fetch(url + `/delete?file=*`);
	if (document.querySelector("#fileTable")) await loadFilesAndFooter();
	if (document.querySelector("#statusInfo")) await loadStatus();
}

// Attach listeners conditionally depending on which page is loaded
window.addEventListener("load", () => {
	const refresh = document.getElementById("refreshBtn");
	const deleteAllBtn = document.getElementById("deleteAllBtn");

	if (document.querySelector("#fileTable")) {
		if (refresh) refresh.addEventListener("click", loadFilesAndFooter);
		if (deleteAllBtn) deleteAllBtn.style.display = "none";
		loadFilesAndFooter();

		const toggleBtn = document.getElementById("toggleLogBtn");
		if (toggleBtn) {
			if (!toggleBtn.dataset.orig) toggleBtn.dataset.orig = toggleBtn.innerHTML;
			toggleBtn.addEventListener("click", async () => {
				const newMode = window.currentLoggingActive ? 1 : 2;
				setButtonLoading(toggleBtn, true, "Bitte warten...");
				try {
					const r = await fetch(url + `/setlogactive?logActive=${newMode}`);
					if (r.ok) {
						await loadFilesAndFooter();
						showSnackbar("OK", "success", 2000);
					} else showSnackbar("Fehler beim Umschalten", "error");
				} catch (e) {
					showSnackbar("Netzwerkfehler", "error");
				}
				setButtonLoading(toggleBtn, false);
			});
		}
	}

	if (document.querySelector("#statusInfo")) {
		if (refresh) refresh.addEventListener("click", loadStatus);
		if (deleteAllBtn) deleteAllBtn.addEventListener("click", deleteAll);
		loadStatus();
		// attach save handler for log mode
		const saveBtn = document.getElementById("saveLogMode");
		if (saveBtn) {
			if (!saveBtn.dataset.orig) saveBtn.dataset.orig = saveBtn.innerHTML;
			saveBtn.addEventListener("click", async () => {
				const sel = document.querySelector(
					"#logModeForm input[name=logmode]:checked"
				);
				const mode = sel.value;
				setButtonLoading(saveBtn, true, "Speichern...");
				try {
					const r = await fetch(url + `/setlogmode?logMode=${mode}`);
					if (r.ok) {
						showSnackbar("Gespeichert", "success");
						await loadStatus();
						await loadFooter();
					} else showSnackbar("Fehler beim Speichern", "error");
				} catch (err) {
					showSnackbar("Netzwerkfehler", "error");
				}
				setButtonLoading(saveBtn, false);
			});
		}
	}

	// mark active nav link
	const links = document.querySelectorAll(".nav-link");
	links.forEach((a) => {
		const href = a.getAttribute("href");
		const path = location.pathname.split("/").pop() || "index.html";
		//  if ((path === '' || path === 'index.html') && href === 'index.html') a.classList.add('active');
		if (path === href) a.classList.add("active");
	});

	// start periodic footer refresh (once per page)
	if (!window._footerIntervalID && document.querySelector("#footer")) {
		window._footerIntervalID = setInterval(() => {
			try {
				loadFooter();
			} catch (e) {}
		}, 3000);
	}
});

// clear interval on unload
window.addEventListener("beforeunload", () => {
	if (window._footerIntervalID) {
		clearInterval(window._footerIntervalID);
		window._footerIntervalID = 0;
	}
});
