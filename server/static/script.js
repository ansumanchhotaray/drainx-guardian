document.addEventListener('DOMContentLoaded', () => {
    // Connect to WebSocket
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;

    let ws;

    function connectWebSocket() {
        ws = new WebSocket(wsUrl);

        ws.onopen = () => {
            console.log('Connected to WebSocket');
            document.querySelector('.status-indicator span').textContent = 'System Online';
            document.querySelector('.dot').classList.add('pulse');
            document.querySelector('.dot').style.backgroundColor = 'var(--success)';
        };

        ws.onmessage = (event) => {
            const data = JSON.parse(event.data);
            updateDashboard(data);
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

            // Logic for progress bar and color (assuming 0-100 scale for demo)
            const percent = Math.min(Math.max((val / 100) * 100, 0), 100);
            document.getElementById('prog-turbidity').style.width = `${percent}%`;

            const card = document.getElementById('card-turbidity');
            card.classList.remove('status-normal', 'status-warning', 'status-danger');
            if (val < 30) card.classList.add('status-normal');
            else if (val < 70) card.classList.add('status-warning');
            else card.classList.add('status-danger');
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

        // Water Level
        if (data.water_level_cm !== undefined) {
            const val = parseFloat(data.water_level_cm).toFixed(1);
            document.getElementById('val-water').textContent = val;

            // Assuming max depth is 200cm
            const percent = Math.min((val / 200) * 100, 100);
            document.getElementById('prog-water').style.width = `${percent}%`;

            const card = document.getElementById('card-water');
            card.classList.remove('status-normal', 'status-warning', 'status-danger');
            if (val > 50) card.classList.add('status-normal'); // Deep water is good?
            else if (val > 20) card.classList.add('status-warning');
            else card.classList.add('status-danger'); // Too shallow/overflowing depending on sensor mount
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

    // Camera Connection Logic
    const connectBtn = document.getElementById('connect-cam');
    const ipInput = document.getElementById('cam-ip');
    const camStream = document.getElementById('camera-stream');

    connectBtn.addEventListener('click', async () => {
        const ip = ipInput.value.trim();
        if (!ip) {
            alert("Please enter the ESP32-CAM IP address.");
            return;
        }

        const rawStreamUrl = `http://${ip}:81/stream`;
        connectBtn.textContent = 'Connecting...';
        connectBtn.disabled = true;

        try {
            // Tell the Python backend to start pulling & processing the stream
            const resp = await fetch('/api/camera', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ url: rawStreamUrl })
            });
            const data = await resp.json();

            if (data.status === 'ok') {
                // Point the img tag at the server's processed MJPEG feed
                // (annotated with red garbage-detection boxes by YOLOv8)
                camStream.src = `/video_feed?t=${Date.now()}`;
                document.querySelector('.target-box').style.display = 'block';
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
});
