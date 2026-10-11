document.addEventListener('DOMContentLoaded', () => {
    // Connect to WebSocket
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;

    let ws;
    let cameraStreamActive = false;

    // Elements
    const camStream = document.getElementById('camera-stream');
    const camDot = document.getElementById('cam-dot');
    const camDetail = document.getElementById('cam-detail');
    const camMacBadge = document.getElementById('cam-mac-badge');

    const sensorDot = document.getElementById('sensor-dot');
    const sensorDetail = document.getElementById('sensor-detail');
    const sensorMacBadge = document.getElementById('sensor-mac-badge');

    // Manual controls toggles
    const toggleCamBtn = document.getElementById('toggle-cam-manual');
    const camManualControls = document.getElementById('cam-manual-controls');
    if (toggleCamBtn && camManualControls) {
        toggleCamBtn.addEventListener('click', () => {
            const isHidden = camManualControls.classList.toggle('hidden');
            toggleCamBtn.textContent = isHidden ? 'Manual IP Override ▾' : 'Hide Manual Override ▴';
        });
    }

    const toggleSensorBtn = document.getElementById('toggle-sensor-manual');
    const sensorManualControls = document.getElementById('sensor-manual-controls');
    if (toggleSensorBtn && sensorManualControls) {
        toggleSensorBtn.addEventListener('click', () => {
            const isHidden = sensorManualControls.classList.toggle('hidden');
            toggleSensorBtn.textContent = isHidden ? 'Manual IP Override ▾' : 'Hide Manual Override ▴';
        });
    }

    function updateNodes(nodes) {
        if (!nodes) return;

        // --- Camera Node ---
        if (nodes.camera) {
            const cam = nodes.camera;
            if (cam.status === 'online') {
                camDot.className = 'status-dot online';
                camDetail.textContent = `Connected (${cam.ip})`;
                camMacBadge.textContent = `MAC: ${cam.mac}`;

                // Automatically activate live stream if not already active
                if (!cameraStreamActive) {
                    cameraStreamActive = true;
                    camStream.src = `/video_feed?t=${Date.now()}`;
                    const targetBox = document.querySelector('.target-box');
                    if (targetBox) targetBox.style.display = 'block';
                }
            } else {
                camDot.className = 'status-dot offline';
                camDetail.textContent = `Offline (${cam.ip || 'ESP32-CAM'})`;
                camMacBadge.textContent = `MAC: ${cam.mac}`;
            }
        } else {
            camDot.className = 'status-dot searching';
            camDetail.textContent = 'Scanning network for ESP32-CAM...';
            camMacBadge.textContent = 'MAC: Auto';
        }

        // --- Sensor Node ---
        if (nodes.sensor) {
            const sensor = nodes.sensor;
            if (sensor.status === 'online') {
                sensorDot.className = 'status-dot online';
                sensorDetail.textContent = `Connected (${sensor.ip})`;
                sensorMacBadge.textContent = `MAC: ${sensor.mac}`;
            } else {
                sensorDot.className = 'status-dot offline';
                sensorDetail.textContent = `Offline (${sensor.ip || 'Sensor Node'})`;
                sensorMacBadge.textContent = `MAC: ${sensor.mac}`;
            }
        } else {
            sensorDot.className = 'status-dot searching';
            sensorDetail.textContent = 'Scanning network for Sensor Node...';
            sensorMacBadge.textContent = 'MAC: Auto';
        }
    }

    // Initial query for currently discovered nodes
    fetch('/api/nodes')
        .then(res => res.json())
        .then(nodes => updateNodes(nodes))
        .catch(err => console.log('Could not fetch nodes initial state:', err));

    function connectWebSocket() {
        ws = new WebSocket(wsUrl);

        ws.onopen = () => {
            console.log('Connected to WebSocket');
            document.querySelector('.status-indicator span').textContent = 'System Online';
            document.querySelector('.dot').classList.add('pulse');
            document.querySelector('.dot').style.backgroundColor = 'var(--success)';
        };

        ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);

                if (data.type === 'initial_state') {
                    if (data.nodes) updateNodes(data.nodes);
                    if (data.sensor_data) updateDashboard(data.sensor_data);
                } else if (data.type === 'node_update') {
                    if (data.nodes) updateNodes(data.nodes);
                } else {
                    // Sensor data or other broadcast
                    updateDashboard(data);
                }
            } catch (e) {
                console.error('Error parsing WebSocket message:', e);
            }
        };

        ws.onclose = () => {
            console.log('WebSocket disconnected. Reconnecting in 3s...');
            document.querySelector('.status-indicator span').textContent = 'System Offline';
            document.querySelector('.dot').classList.remove('pulse');
            document.querySelector('.dot').style.backgroundColor = 'var(--danger)';
            setTimeout(connectWebSocket, 3000);
        };
    }

    connectWebSocket();

    // Update UI with sensor data
    function updateDashboard(data) {
        // Turbidity
        if (data.turbidity !== undefined) {
            const val = parseFloat(data.turbidity).toFixed(1);
            document.getElementById('val-turbidity').textContent = val;
            if (data.turbidity_status) {
                document.getElementById('val-turbidity-status').textContent = data.turbidity_status;
            }

            // Logic for progress bar and color (0-150 scale based on sensor logic)
            const percent = Math.min(Math.max((val / 150) * 100, 0), 100);
            document.getElementById('prog-turbidity').style.width = `${percent}%`;

            const card = document.getElementById('card-turbidity');
            card.classList.remove('status-normal', 'status-warning', 'status-danger');
            if (data.turbidity_class) {
                card.classList.add(data.turbidity_class);
            }
        }

        // pH Level
        if (data.ph !== undefined) {
            const val = parseFloat(data.ph).toFixed(2);
            document.getElementById('val-ph').textContent = val;

            // pH scale 0-14
            const percent = (val / 14) * 100;
            document.getElementById('prog-ph').style.width = `${percent}%`;

            const card = document.getElementById('card-ph');
            card.classList.remove('status-normal', 'status-warning', 'status-danger');
            if (val >= 6.5 && val <= 8.5) card.classList.add('status-normal');
            else if ((val >= 5 && val < 6.5) || (val > 8.5 && val <= 10)) card.classList.add('status-warning');
            else card.classList.add('status-danger');
        }

        // Temperature
        if (data.temperature !== undefined) {
            const val = parseFloat(data.temperature).toFixed(1);
            document.getElementById('val-temp').textContent = val;

            // Assuming 0-50C range
            const percent = Math.min(Math.max((val / 50) * 100, 0), 100);
            document.getElementById('prog-temp').style.width = `${percent}%`;

            const card = document.getElementById('card-temp');
            card.classList.remove('status-normal', 'status-warning', 'status-danger');
            if (val >= 10 && val <= 30) card.classList.add('status-normal');
            else if (val > 30 && val <= 40) card.classList.add('status-warning');
            else card.classList.add('status-danger');
        }
    }

    // Manual Camera Connection Logic (Fallback)
    const connectBtn = document.getElementById('connect-cam');
    const ipInput = document.getElementById('cam-ip');

    if (connectBtn && ipInput) {
        connectBtn.addEventListener('click', async () => {
            const ip = ipInput.value.trim();
            if (!ip) {
                alert("Please enter the ESP32-CAM IP address.");
                return;
            }

            const snapshotUrl = `http://${ip}/capture`;
            connectBtn.textContent = 'Connecting...';
            connectBtn.disabled = true;

            try {
                const resp = await fetch('/api/camera', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ url: snapshotUrl })
                });
                const data = await resp.json();

                if (data.status === 'ok') {
                    cameraStreamActive = true;
                    camStream.src = `/video_feed?t=${Date.now()}`;
                    const targetBox = document.querySelector('.target-box');
                    if (targetBox) targetBox.style.display = 'block';
                    connectBtn.textContent = 'Reconnect';
                } else {
                    alert(`Server error: ${data.message}`);
                    connectBtn.textContent = 'Connect Stream';
                }
            } catch (e) {
                alert("Could not reach the Python server. Is it running?");
                connectBtn.textContent = 'Connect Stream';
            }
            connectBtn.disabled = false;
        });
    }

    // Manual Sensor Connection Logic (Fallback)
    const connectSensorBtn = document.getElementById('connect-sensor');
    const sensorIpInput = document.getElementById('sensor-ip');

    if (connectSensorBtn && sensorIpInput) {
        connectSensorBtn.addEventListener('click', async () => {
            const ip = sensorIpInput.value.trim();
            if (!ip) {
                alert("Please enter the ESP32 Sensor Node IP address.");
                return;
            }

            const sensorUrl = `http://${ip}/sensors`;
            connectSensorBtn.textContent = 'Connecting...';
            connectSensorBtn.disabled = true;

            try {
                const resp = await fetch('/api/set_sensor_node', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ url: sensorUrl })
                });
                const data = await resp.json();

                if (data.status === 'ok') {
                    connectSensorBtn.textContent = 'Connected';
                    connectSensorBtn.style.backgroundColor = 'var(--success)';
                } else {
                    alert(`Server error: ${data.message}`);
                    connectSensorBtn.textContent = 'Connect Sensors';
                }
            } catch (e) {
                alert("Could not reach the Python server. Is it running?");
                connectSensorBtn.textContent = 'Connect Sensors';
            }
            connectSensorBtn.disabled = false;
        });
    }
});
