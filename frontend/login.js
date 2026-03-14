/* ═══════════════════════════════════════════════════════════════════
   Chennai Transit — Login Logic (login.js)
   ═══════════════════════════════════════════════════════════════════ */

const API = '';

document.getElementById('login-form').addEventListener('submit', async (e) => {
    e.preventDefault();

    const username = document.getElementById('inp-username').value.trim();
    const password = document.getElementById('inp-password').value.trim();
    const errorEl  = document.getElementById('login-error');
    const btn      = document.getElementById('btn-login');

    if (!username || !password) {
        showError('Please enter both username and password');
        return;
    }

    // Disable button while loading
    btn.disabled = true;
    btn.textContent = 'Signing in...';
    errorEl.style.display = 'none';

    try {
        const res = await fetch(`${API}/api/auth/login`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, password }),
        });

        const data = await res.json();

        if (!res.ok) {
            showError(data.detail || 'Invalid credentials');
            return;
        }

        // Store user info in sessionStorage
        sessionStorage.setItem('user_role', data.role);
        sessionStorage.setItem('user_name', data.name);

        // Role-based redirection
        if (data.role === 'admin') {
            window.location.href = '/app/home.html';
        } else if (data.role === 'driver') {
            window.location.href = '/app/driver.html';
        }

    } catch (err) {
        showError('Cannot connect to server. Please try again.');
    } finally {
        btn.disabled = false;
        btn.textContent = 'Sign In';
    }
});

function showError(msg) {
    const el = document.getElementById('login-error');
    el.textContent = msg;
    el.style.display = 'block';
}
