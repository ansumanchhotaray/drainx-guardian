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

    const motorDot = document.getElementById('motor-dot');
    const motorDetail = document.getElementById('motor-detail');
    const motorMacBadge = document.getElementById('motor-mac-badge');
    const motorBadge = document.getElementById('motor-badge');
    const motorControls = document.getElementById('motor-controls');

    const armDot = document.getElementById('arm-dot');
    const armDetail = document.getElementById('arm-detail');
    const armMacBadge = document.getElementById('arm-mac-badge');
    const armBadge = document.getElementById('arm-badge');
    const armControls = document.getElementById('arm-controls');
    const armSlider = document.getElementById('arm-slider');
    const armAngleVal = document.getElementById('arm-angle-val');
    let isUserDraggingArm = false;

    const debrisBadge = document.getElementById('debris-badge');
    const aiVisionBadge = document.getElementById('ai-vision-badge');
    const annotationLayer = document.getElementById('annotation-layer');
    const videoSection = document.querySelector('.video-section');

    // Autonomous Garbage Collection elements
    const autoModePanel = document.getElementById('auto-mode-panel');
    const autoModeBtn = document.getElementById('auto-mode-btn');
    const autoBtnText = document.getElementById('auto-btn-text');
    const autoModeStatus = document.getElementById('auto-mode-status');
    const autoPillVision = document.getElementById('auto-pill-vision');
    const autoPillWheels = document.getElementById('auto-pill-wheels');
    const autoPillArm = document.getElementById('auto-pill-arm');
    let autoModeEnabled = false;

    function highlightActiveActuators(motorDir, armAngle) {
        if (motorDir !== undefined) {
            document.querySelectorAll('.motor-btn-row button').forEach(btn => {
                const btnDir = btn.dataset.dir || 's';
                if (btnDir === motorDir) {
                    btn.classList.add('active-motor-cmd');
                } else {
                    btn.classList.remove('active-motor-cmd');
                }
            });
        }
        if (armAngle !== undefined) {
            document.querySelectorAll('.arm-preset').forEach(btn => {
                if (parseInt(btn.dataset.angle, 10) === parseInt(armAngle, 10)) {
                    btn.classList.add('active-preset');
                } else {
                    btn.classList.remove('active-preset');
                }
            });
        }
    }

    function updateAutoState(auto) {
        if (!auto) return;
        autoModeEnabled = Boolean(auto.enabled);

        if (autoModeBtn) {
            autoModeBtn.setAttribute('aria-pressed', autoModeEnabled ? 'true' : 'false');
            autoModeBtn.classList.toggle('active', autoModeEnabled);
        }
        if (autoBtnText) {
            autoBtnText.textContent = autoModeEnabled ? 'Auto Mode: ON' : 'Enable Auto Mode';
        }

        if (autoModePanel) {
            autoModePanel.classList.remove('auto-active', 'auto-collecting');
            if (autoModeEnabled) {
                if (auto.state === 'picking_up' || auto.debris_detected) {
                    autoModePanel.classList.add('auto-collecting');
                } else {
                    autoModePanel.classList.add('auto-active');
                }
            }
        }

        if (autoModeStatus) {
            if (!autoModeEnabled) {
                autoModeStatus.textContent = 'Off — Manual Control (Arm Default: 90°)';
            } else if (auto.state === 'moving') {
                autoModeStatus.textContent = '✅ Garbage Picked Up (180°) — Moving Forward!';
            } else if (auto.state === 'picking_up' || auto.debris_detected) {
                autoModeStatus.textContent = '⚠️ Garbage Detected! Raising Arm to 180° to Pick Up...';
            } else {
                autoModeStatus.textContent = 'Auto Active — Waiting for Garbage • Wheels Stopped • Arm at 90°';
            }
        }

        // Update telemetry pills
        if (autoPillVision) {
            if (auto.debris_detected) {
                autoPillVision.textContent = '📷 Garbage Detected!';
                autoPillVision.className = 'auto-pill pill-alert';
            } else if (autoModeEnabled && (auto.state === 'picking_up' || auto.state === 'moving')) {
                autoPillVision.textContent = '📷 Garbage Collected';
                autoPillVision.className = 'auto-pill pill-ok';
            } else {
                autoPillVision.textContent = '📷 Camera: Clear';
                autoPillVision.className = 'auto-pill pill-ok';
            }
        }

        if (autoPillWheels) {
            const dir = auto.motor_dir || 's';
            if (dir === 'f') {
                autoPillWheels.textContent = '⚙️ Wheels: Moving Forward';
                autoPillWheels.className = 'auto-pill pill-ok';
            } else if (dir === 'b') {
                autoPillWheels.textContent = '⚙️ Wheels: Moving Back';
                autoPillWheels.className = 'auto-pill';
            } else if (autoModeEnabled && auto.state === 'picking_up') {
                autoPillWheels.textContent = '⚙️ Wheels: Waiting (Pickup)';
                autoPillWheels.className = 'auto-pill pill-alert';
            } else {
                autoPillWheels.textContent = '⚙️ Wheels: Stopped';
                autoPillWheels.className = 'auto-pill';
            }
        }

        if (autoPillArm) {
            const angle = auto.arm_angle !== undefined ? auto.arm_angle : 90;
            if (angle >= 170) {
                if (autoModeEnabled && auto.state === 'moving') {
                    autoPillArm.textContent = `🦾 Arm: ${angle}° (Picked Up)`;
                    autoPillArm.className = 'auto-pill pill-ok';
                } else {
                    autoPillArm.textContent = `🦾 Arm: ${angle}° (Picking Up)`;
                    autoPillArm.className = 'auto-pill pill-alert';
                }
            } else if (angle === 90) {
                autoPillArm.textContent = '🦾 Arm: 90° (Default)';
                autoPillArm.className = 'auto-pill pill-ok';
            } else {
                autoPillArm.textContent = `🦾 Arm: ${angle}°`;
                autoPillArm.className = 'auto-pill';
            }
        }

        if (auto.arm_angle !== undefined && !isUserDraggingArm) {
            if (armSlider) armSlider.value = auto.arm_angle;
            if (armAngleVal) armAngleVal.textContent = `${auto.arm_angle}°`;
        }

        highlightActiveActuators(auto.motor_dir, auto.arm_angle);
    }

    if (autoModeBtn) {
        autoModeBtn.addEventListener('click', async () => {
            const targetEnabled = !autoModeEnabled;
            autoModeBtn.disabled = true;
            try {
                const resp = await fetch('/api/auto', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ enabled: targetEnabled })
                });
                const data = await resp.json();
                if (data.status === 'ok' && data.auto) {
                    updateAutoState(data.auto);
                }
            } catch (e) {
                console.error('Failed to toggle Auto Mode:', e);
            } finally {
                autoModeBtn.disabled = false;
            }
        });
    }

    function updateVisionStatus(vision) {
        if (!vision) return;

        // 1. AI model status badge
        if (aiVisionBadge) {
            if (vision.model_ready === false) {
                aiVisionBadge.textContent = 'AI Initializing...';
                aiVisionBadge.className = 'badge loading';
            } else {
                aiVisionBadge.textContent = 'AI Vision Active';
                aiVisionBadge.className = 'badge';
            }
        }

        // 2. Debris detection status badge & container red tint alert
        if (debrisBadge) {
            if (vision.debris_detected) {
                debrisBadge.className = 'debris-badge detected';
                const itemsText = vision.items && vision.items.length ? `: ${vision.items.join(', ')}` : '';
                debrisBadge.textContent = `⚠️ Debris Detected${itemsText}`;
                if (videoSection) videoSection.classList.add('debris-alert');
            } else {
                debrisBadge.className = 'debris-badge clear';
                debrisBadge.textContent = '✓ Clear';
                if (videoSection) videoSection.classList.remove('debris-alert');
            }
        }

        // 3. Web frontend bounding box annotations overlay
        if (annotationLayer) {
            annotationLayer.innerHTML = '';
            if (vision.boxes && vision.boxes.length > 0) {
                vision.boxes.forEach(box => {
                    const boxEl = document.createElement('div');
                    boxEl.className = 'vision-box';
                    boxEl.style.left = `${box.x}%`;
                    boxEl.style.top = `${box.y}%`;
                    boxEl.style.width = `${box.w}%`;
                    boxEl.style.height = `${box.h}%`;
                    if (box.color) boxEl.style.borderColor = box.color;

                    const labelEl = document.createElement('span');
                    labelEl.className = 'vision-box-label';
                    if (box.color) labelEl.style.backgroundColor = box.color;
                    if (box.y < 8) {
                        labelEl.style.top = '2px';
                        labelEl.style.bottom = 'auto';
                    }
                    labelEl.textContent = `${box.label} ${box.conf}%`;

                    boxEl.appendChild(labelEl);
                    annotationLayer.appendChild(boxEl);
                });
            }
        }
    }

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

                // Automatically activate or refresh live stream if not active or IP changed
                if (!cameraStreamActive || camStream.dataset.activeIp !== cam.ip) {
                    cameraStreamActive = true;
                    camStream.dataset.activeIp = cam.ip;
                    camStream.src = `/video_feed?t=${Date.now()}`;
                }
            } else {
                cameraStreamActive = false;
                camDot.className = 'status-dot offline';
                camDetail.textContent = `Offline (${cam.ip || 'ESP32-CAM'})`;
                camMacBadge.textContent = `MAC: ${cam.mac}`;
            }
        } else {
            cameraStreamActive = false;
            camDot.className = 'status-dot searching';
            camDetail.textContent = 'Scanning...';
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
            sensorDetail.textContent = 'Scanning...';
            sensorMacBadge.textContent = 'MAC: Auto';
        }

        // --- Motor Node ---
        if (nodes.motor) {
            const motor = nodes.motor;
            if (motor.status === 'online') {
                if (motorDot) motorDot.className = 'status-dot online';
                if (motorDetail) motorDetail.textContent = `Connected (${motor.ip})`;
                if (motorMacBadge) motorMacBadge.textContent = `MAC: ${motor.mac}`;
                if (motorBadge) motorBadge.textContent = 'Online';
                if (motorControls) {
                    motorControls.style.opacity = '1';
                    motorControls.style.pointerEvents = 'auto';
                }
            } else {
                if (motorDot) motorDot.className = 'status-dot offline';
                if (motorDetail) motorDetail.textContent = `Offline (${motor.ip || 'Motor Node'})`;
                if (motorMacBadge) motorMacBadge.textContent = `MAC: ${motor.mac}`;
                if (motorBadge) motorBadge.textContent = 'Offline';
                if (motorControls) {
                    motorControls.style.opacity = '0.4';
                    motorControls.style.pointerEvents = 'none';
                }
            }
        } else {
            if (motorDot) motorDot.className = 'status-dot searching';
            if (motorDetail) motorDetail.textContent = 'Scanning...';
            if (motorMacBadge) motorMacBadge.textContent = 'MAC: Auto';
            if (motorBadge) motorBadge.textContent = 'Searching...';
        }

        // --- Arm Node (CameraAndArmNode over LAN) ---
        if (nodes.arm) {
            const arm = nodes.arm;
            if (arm.status === 'online') {
                if (armDot) armDot.className = 'status-dot online';
                if (armDetail) armDetail.textContent = `Connected (${arm.ip})`;
                if (armMacBadge) armMacBadge.textContent = `MAC: ${arm.mac}`;
                if (armBadge) armBadge.textContent = 'Online';
                if (armControls) {
                    armControls.style.opacity = '1';
                    armControls.style.pointerEvents = 'auto';
                }
            } else {
                if (armDot) armDot.className = 'status-dot offline';
                if (armDetail) armDetail.textContent = `Offline (${arm.ip || 'Arm Node'})`;
                if (armMacBadge) armMacBadge.textContent = `MAC: ${arm.mac || 'Auto'}`;
                if (armBadge) armBadge.textContent = 'Offline';
                if (armControls) {
                    armControls.style.opacity = '0.4';
                    armControls.style.pointerEvents = 'none';
                }
            }
            if (arm.angle !== undefined && !isUserDraggingArm) {
                if (armSlider) armSlider.value = arm.angle;
                if (armAngleVal) armAngleVal.textContent = `${arm.angle}°`;
            }
        } else {
            if (armDot) armDot.className = 'status-dot searching';
            if (armDetail) armDetail.textContent = 'Scanning...';
            if (armMacBadge) armMacBadge.textContent = 'MAC: Auto';
            if (armBadge) armBadge.textContent = 'Searching...';
        }

        // --- Vision Debris Detection ---
        if (nodes.vision) {
            updateVisionStatus(nodes.vision);
        }

        // --- Autonomous Mode Status ---
        if (nodes.auto) {
            updateAutoState(nodes.auto);
        }
    }

    // Initial query for currently discovered nodes
    fetch('/api/nodes')
        .then(res => res.json())
        .then(nodes => updateNodes(nodes))
        .catch(err => console.log('Could not fetch nodes initial state:', err));

    // Periodic polling to guarantee dashboard status is always fresh (every 2.5s)
    setInterval(() => {
        fetch('/api/nodes')
            .then(res => res.json())
            .then(nodes => updateNodes(nodes))
            .catch(() => {});
    }, 2500);

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
                    if (data.auto) updateAutoState(data.auto);
                } else if (data.type === 'node_update') {
                    if (data.nodes) updateNodes(data.nodes);
                    if (data.auto) updateAutoState(data.auto);
                } else if (data.type === 'vision_update') {
                    if (data.vision) updateVisionStatus(data.vision);
                    if (data.auto) updateAutoState(data.auto);
                } else if (data.type === 'arm_update') {
                    if (data.angle !== undefined && !isUserDraggingArm) {
                        if (armSlider) armSlider.value = data.angle;
                        if (armAngleVal) armAngleVal.textContent = `${data.angle}°`;
                    }
                    if (data.nodes) updateNodes(data.nodes);
                    if (data.auto) updateAutoState(data.auto);
                } else if (data.type === 'motor_update') {
                    if (data.auto) updateAutoState(data.auto);
                    else highlightActiveActuators(data.dir, undefined);
                } else if (data.type === 'auto_update') {
                    if (data.auto) updateAutoState(data.auto);
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

    // --- Motor Control Handlers ---
    const motorSpeedSelect = document.getElementById('motor-speed');

    window.sendMotor = async function(dir, dur = 0) {
        const speed = motorSpeedSelect ? parseInt(motorSpeedSelect.value, 10) : 175;
        try {
            await fetch('/api/motor', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ dir: dir, dur: dur, speed: speed })
            });
        } catch (e) {
            console.error('Motor command error:', e);
        }
    };

    if (motorSpeedSelect) {
        motorSpeedSelect.addEventListener('change', async () => {
            const speed = parseInt(motorSpeedSelect.value, 10);
            try {
                await fetch('/api/motor_speed', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ speed: speed })
                });
            } catch (e) {
                console.error('Motor speed update error:', e);
            }
        });
    }

    document.querySelectorAll('.motor-btn').forEach(btn => {
        const dir = btn.dataset.dir;
        let isPressed = false;

        const stopPress = () => {
            if (!isPressed) return;
            isPressed = false;
            btn.style.opacity = '1';
            sendMotor('s');
        };

        btn.addEventListener('pointerdown', (e) => {
            e.preventDefault();
            if (isPressed) return;
            isPressed = true;
            try { btn.setPointerCapture(e.pointerId); } catch (_) {}
            btn.style.opacity = '0.7';
            sendMotor(dir);
        });
        btn.addEventListener('pointerup', stopPress);
        btn.addEventListener('pointercancel', stopPress);
        btn.addEventListener('lostpointercapture', stopPress);
    });

    // Manual Motor Connection Logic
    const toggleMotorBtn = document.getElementById('toggle-motor-manual');
    const motorManualControls = document.getElementById('motor-manual-controls');
    if (toggleMotorBtn && motorManualControls) {
        toggleMotorBtn.addEventListener('click', () => {
            const isHidden = motorManualControls.classList.toggle('hidden');
            toggleMotorBtn.textContent = isHidden ? 'Manual IP Override ▾' : 'Hide Manual Override ▴';
        });
    }

    const connectMotorBtn = document.getElementById('connect-motor');
    const motorIpInput = document.getElementById('motor-ip');
    if (connectMotorBtn && motorIpInput) {
        connectMotorBtn.addEventListener('click', async () => {
            const ip = motorIpInput.value.trim();
            if (!ip) {
                alert("Please enter the Motor Node IP address.");
                return;
            }

            connectMotorBtn.textContent = 'Connecting...';
            connectMotorBtn.disabled = true;

            try {
                const resp = await fetch('/api/set_motor_node', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ip: ip })
                });
                const data = await resp.json();

                if (data.status === 'ok') {
                    connectMotorBtn.textContent = 'Connected';
                    connectMotorBtn.style.backgroundColor = 'var(--success)';
                    if (motorControls) {
                        motorControls.style.opacity = '1';
                        motorControls.style.pointerEvents = 'auto';
                    }
                } else {
                    alert(`Server error: ${data.message}`);
                    connectMotorBtn.textContent = 'Connect Motor';
                }
            } catch (e) {
                alert("Could not reach the Python server.");
                connectMotorBtn.textContent = 'Connect Motor';
            }
            connectMotorBtn.disabled = false;
        });
    }

    // --- Arm Node Control Handlers (Robotic Collector Arm - Arduino Uno) ---
    let lastArmSendTime = 0;
    let pendingArmTimeout = null;

    async function sendArmAngle(angle) {
        angle = parseInt(angle, 10);
        if (isNaN(angle)) return;
        angle = Math.max(0, Math.min(180, angle));
        if (armAngleVal) armAngleVal.textContent = `${angle}°`;

        try {
            await fetch('/api/arm', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ angle: angle })
            });
        } catch (e) {
            console.error('Arm command error:', e);
        }
    }

    if (armSlider) {
        armSlider.addEventListener('input', (e) => {
            isUserDraggingArm = true;
            const angle = parseInt(e.target.value, 10);
            if (armAngleVal) armAngleVal.textContent = `${angle}°`;

            // Throttle sending commands to at most once per 60ms
            const now = Date.now();
            if (now - lastArmSendTime > 60) {
                lastArmSendTime = now;
                sendArmAngle(angle);
            } else {
                clearTimeout(pendingArmTimeout);
                pendingArmTimeout = setTimeout(() => {
                    sendArmAngle(angle);
                }, 60);
            }
        });

        armSlider.addEventListener('change', (e) => {
            isUserDraggingArm = false;
            sendArmAngle(e.target.value);
        });
    }

    // Preset angle buttons (0, 45, 90, 135, 180)
    document.querySelectorAll('.arm-preset').forEach(btn => {
        btn.addEventListener('click', () => {
            const angle = parseInt(btn.dataset.angle, 10);
            if (armSlider) armSlider.value = angle;
            sendArmAngle(angle);
        });
    });

    // Nudge buttons (-5 / +5)
    document.querySelectorAll('.arm-nudge').forEach(btn => {
        btn.addEventListener('click', () => {
            const delta = parseInt(btn.dataset.delta, 10);
            const current = armSlider ? parseInt(armSlider.value, 10) : 90;
            const newAngle = Math.max(0, Math.min(180, current + delta));
            if (armSlider) armSlider.value = newAngle;
            sendArmAngle(newAngle);
        });
    });

    // Manual Arm Node Connection Logic
    const toggleArmBtn = document.getElementById('toggle-arm-manual');
    const armManualControls = document.getElementById('arm-manual-controls');
    if (toggleArmBtn && armManualControls) {
        toggleArmBtn.addEventListener('click', () => {
            const isHidden = armManualControls.classList.toggle('hidden');
            toggleArmBtn.textContent = isHidden ? 'Manual IP Override ▾' : 'Hide Manual Override ▴';
        });
    }

    const connectArmBtn = document.getElementById('connect-arm');
    const armIpInput = document.getElementById('arm-ip');
    if (connectArmBtn && armIpInput) {
        connectArmBtn.addEventListener('click', async () => {
            const ip = armIpInput.value.trim();
            if (!ip) {
                alert("Please enter the CameraAndArmNode IP address.");
                return;
            }

            connectArmBtn.textContent = 'Connecting...';
            connectArmBtn.disabled = true;

            try {
                const resp = await fetch('/api/set_arm_node', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ip: ip })
                });
                const data = await resp.json();

                if (data.status === 'ok') {
                    connectArmBtn.textContent = 'Connected';
                    connectArmBtn.style.backgroundColor = 'var(--success)';
                    if (armControls) {
                        armControls.style.opacity = '1';
                        armControls.style.pointerEvents = 'auto';
                    }
                } else {
                    alert(`Error: ${data.message || 'Could not connect'}`);
                    connectArmBtn.textContent = 'Connect';
                }
            } catch (e) {
                alert("Could not reach Python server.");
                connectArmBtn.textContent = 'Connect';
            }
            connectArmBtn.disabled = false;
        });
    }
});
