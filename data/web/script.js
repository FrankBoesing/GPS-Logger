const debug = (location.hostname === "localhost");
const url = debug ? "http://gps" : "";
const tz = new Date().getTimezoneOffset() * -60;
const doc = document;
const $ = (q) => doc.querySelector(q);
const $$ = (q) => doc.querySelectorAll(q);
const pad = (n) => String(n).padStart(2, "0");
let data = {}, es;

// Zeit-Helfer
const getDateParts = (t) => {
	const d = new Date(1000 * t);
	return { Y: d.getFullYear(), M: pad(d.getMonth() + 1), D: pad(d.getDate()), h: pad(d.getHours()), m: pad(d.getMinutes()) };
};

const fmtTime = (t) => {
	const { D, M, Y, h, m } = getDateParts(t);
	return `${D}.${M}.${Y} ${h}:${m}`;
};

const fmtHM = (s) => {
	const sec = Math.max(0, s);
	return `${pad((sec / 3600) | 0)}:${pad(((sec % 3600) / 60) | 0)}`;
};

function showSnackbar(msg, type = "", duration = 3000) {
	let sb = $("#snackbar") || Object.assign(doc.createElement("div"), { id: "snackbar" });
	if (!sb.parentElement) doc.body.appendChild(sb);
	const icon = type === "success" ? "✅ " : (type === "error" ? "❌ " : "ℹ️ ");
	sb.className = `snackbar ${type} show`;
	sb.innerHTML = `<span style="display:flex; align-items:center; gap:8px;">${icon}${msg}</span>`;
	clearTimeout(sb._t);
	sb._t = setTimeout(() => sb.classList.remove("show"), duration);
}

function switchView(targetId) {
	$$('.view').forEach(v => v.style.display = 'none');
	$(`#${targetId}`).style.display = 'block';
	$$('.nav-link').forEach(l => l.classList.remove('active'));
	$(`[data-target="${targetId}"]`)?.classList.add('active');
}
// API & Daten
async function downloadFile(dlUrl) {
	const dl = $('#download');
	dl.style.display = 'block';
	dl.textContent = 'Start...';
	try {
		const tMatch = dlUrl.match(/file=.*?(\d+)/) || dlUrl.match(/\d+/);
		const tVal = tMatch ? parseInt(tMatch[1] || tMatch[0], 10) : (Date.now() / 1000);

		const { Y, M, D, h, m } = getDateParts(tVal + tz);
		const fname = `${Y}-${M}-${D}_${h}-${m}.gpx`.trim();

		const resp = await fetch(dlUrl);
		if (!resp.ok) throw new Error(resp.status);

		const reader = resp.body.getReader(), chunks = [];
		const total = parseInt(resp.headers.get('Content-Length') || 0, 10);
		let received = 0;

		while (true) {
			const { done, value } = await reader.read();
			if (done) break;
			chunks.push(value);
			received += value.length;
			const kb = (received / 1024).toFixed(0);
			dl.textContent = total ? `${kb}KB (${((received / total) * 100).toFixed(0)}%)` : `${kb}KB...`;
		}

		const href = URL.createObjectURL(new Blob(chunks, { type: 'application/octet-stream' }));
		const a = Object.assign(doc.createElement('a'), { href, download: fname });
		doc.body.appendChild(a);
		a.click();
		a.remove();

		setTimeout(() => URL.revokeObjectURL(href), 15000);
		dl.textContent = `Fertig (${(received / 1024).toFixed(1)} KB)`;
	} catch (err) {
		dl.textContent = 'Fehler';
		alert('Download fehlgeschlagen');
	} finally {
		setTimeout(() => dl.style.display = 'none', 1200);
	}
}

async function displayFiles() {
	const tbody = $("#fileTable tbody");
	tbody.innerHTML = data.files?.map(f => {
		const [fp, fl, fa] = f;
		const date = fmtTime(fp);
		const dur = fmtHM(fl - fp);

		return `<tr><td>${fa ? `<span class="active-file">⏺ ${date}</span>` : `<a href="#" class="download" data-path="${fp}">⬇️ ${date}</a>`}</td>
            <td class="col-dur">${dur}</td>
            <td class="col-del">${fa ? '' : `<button class="delete" data-path="${fp}">🗑️</button>`}</td></tr>`;
	}).join("\n");
}

async function displayStatus() {
	const rows = [
		["Genauigkeit", data.q + " m"],
		["Erster Fix", (data.firstFix / 1e6).toFixed(0) + " s"],
		["RAM frei", (data.RAMminFree / 1024).toFixed(1) + " KB"],
		["Flash frei", ((data.total - data.used) / 1024).toFixed(0) + " KB"]
	].map(([k, v]) => `<tr><th>${k}</th><td>${v}</td></tr>`).join("\n");

	$("#statusTableWrap").innerHTML = `<table class="status-table">${rows}</table>`;
	$("#autostartToggle").checked = !!data.logMode;
	$("#appendToggle").checked = !!data.logAppend;
}

async function displayWifi() {
	const wifi = $("#wifi");
	wifi.innerHTML = data.wifi?.map((ssid, idx) => {
		return `<div class="wifi-row">
			<input id="wifi${idx}_ssid" placeholder="SSID ${idx + 1}" value="${ssid}"><input id="wifi${idx}_pass" type="password" placeholder="Passwort ${idx + 1}"><button id="saveWifi${idx}Btn">Speichern</button></div>`;
	}).join("");
}

async function displayFooter() {
	const act = !!data.active;
	const free = Math.round(100 - (100 * data.used) / data.total);

	$("#footer").innerHTML = `<div class="status-indicator"><span class="rec ${act ? 'active' : 'inactive'}"></span><span class="status-text">${act ? "AUFNAHME" : "STANDBY"}</span></div>
			<div class="separator">|</div>
			<div class="storage-info">Speicher: <span class="storage-val">${free}% frei</span></div>`;

	$("#build").textContent = fmtTime(data.build);
	const tb = $("#toggleLogBtn");
	if (tb) {
		tb.textContent = act ? "⏹️ Stop Log" : "⏺️ Sofort starten";
		tb.classList.toggle("danger", !act);
	}
}

async function btn(el, k, v) {
	el.disabled = true;
	try {
		const r = await fetch(`${url}/set?${k}=${v}`, { method: 'POST' });
		showSnackbar(r.ok ? "Einstellung gespeichert" : "Fehler am Gerät", r.ok ? "success" : "error");
	} catch (e) {
		showSnackbar("Netzwerkfehler", "error");
	}
	el.disabled = false;
}

async function startSSE() {
	if (es?.readyState === 1) return;
	if (debug) fetch(`${url}/events`, { method: 'OPTIONS' });
	es = new EventSource(url + `/events`);
	es.onmessage = (e) => {
		const d = JSON.parse(e.data);
		for (const key in d) if (d.hasOwnProperty(key)) data[key] = d[key];
		displayFiles();
		displayFooter();
		displayStatus();
		displayWifi();
	};
}

window.addEventListener("load", () => {
	$$('.nav-link').forEach(link => { link.addEventListener('click', () => switchView(link.dataset.target)); });// Navigations-Logik
	$("#settingsGearBtn")?.addEventListener('click', () => switchView('view-settings'));
	$("#autostartToggle")?.addEventListener("change", function () { btn(this, "mode", this.checked ? 1 : 0); });
	$("#appendToggle")?.addEventListener("change", function () { btn(this, "append", this.checked ? 1 : 0); });
	$("#toggleLogBtn")?.addEventListener("click", function () { btn(this, "active", !!data.active ? 1 : 2); });
	$("#deleteAllBtn")?.addEventListener("click", async () => { if (confirm("Alles löschen?")) { fetch(`${url}/delete?file=-1`, { method: 'DELETE' }); } });
	$("#fileTable tbody")?.addEventListener("click", (e) => {
		const t = e.target, p = t.dataset.path;
		if (!p) return;
		if (t.matches(".download")) {
			e.preventDefault();
			downloadFile(`${url}/download?file=${p}`);
		} else if (t.matches(".delete") && confirm(`Löschen?`)) {
			fetch(`${url}/delete?file=${p}`, { method: 'DELETE' });
		}
	});
	$("#wifi")?.addEventListener("click", async ({ target: t }) => { // Wifi / AP save handler
		if (t.tagName !== "BUTTON" || !t.id.startsWith("saveWifi")) return;
		const idx = t.id.match(/\d+/)[0];
		const val = (suffix) => $(`#wifi${idx}_${suffix}`)?.value.trim();
		const ssid = val("ssid");
		const pass = val("pass");
		const del = ssid =="" || pass == "";
		try {
			t.disabled = true;
			const query = new URLSearchParams({ [`wifi${idx}`]: ssid, [`pass${idx}`]: pass });
			const r = await fetch(`${url}/set?${query}`, { method: 'POST' });
			showSnackbar(r.ok ? (del ? "Gelöscht" : "Gespeichert") : "Fehler", r.ok ? "success" : "error");
		} catch (e) {
			showSnackbar("Netzwerkfehler", "error");
		} finally {
			t.disabled = false;
		}
	});

	startSSE();
});
window.addEventListener("beforeunload", () => es?.close());
