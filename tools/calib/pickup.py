"""Pickup model shared by the calibration scripts.

The tine tip sits at a vertical offset u0 from the pickup axis and swings with
amplitude a (both in units of the tine-to-pickup gap).  The flux through the
coil is  Phi(u) = (1 + u^2)^-q  and the output voltage is dPhi/dt, so harmonic
n of the output has amplitude  n * |P_n(a)|  where P_n are the Fourier
coefficients of Phi(u0 + a sin(theta)).
"""
import numpy as np

A_GRID = np.geomspace(0.004, 40.0, 220)


def harmonic_table(u0, q, nharm, a_grid=A_GRID, m=4096):
    """dB level of output harmonics 1..nharm for every amplitude in a_grid."""
    th = 2 * np.pi * np.arange(m) / m
    u = u0 + a_grid[:, None] * np.sin(th)[None, :]
    phi = (1.0 + u * u) ** (-q)
    c = np.fft.rfft(phi, axis=1) / m * 2.0
    n = np.arange(1, nharm + 1)
    amp = np.abs(c[:, 1:nharm + 1]) * n[None, :]
    return 20 * np.log10(np.maximum(amp, 1e-12))


def waveform(u0, q, a, m=4096):
    """One period of the output voltage (per radian), for plotting and checks."""
    th = 2 * np.pi * np.arange(m) / m
    u = u0 + a * np.sin(th)
    return -2 * q * u * (1 + u * u) ** (-q - 1) * a * np.cos(th)
