#!/usr/bin/env python3
"""
train_model.py — Train the RSSI relay classifier from logged data
ใช้งาน: python3 tools/train_model.py rssi_log.csv

Output: include/rssi_classifier.h  ← the header the firmware actually compiles
        (Dense(4→8,ReLU) → Dense(8→4,ReLU) → Dense(4→1,sigmoid), weights inline)

Requires: numpy, pandas.  TensorFlow/sklearn are NOT needed (trains with numpy).

--- Why this script was rewritten -------------------------------------------
The previous version trained with Keras and wrote a TFLite byte array to
`rssi_classifier.h` in the *current directory*.  Nothing in the firmware ever
referenced that array — main.cpp includes `include/rssi_classifier.h`, which
holds hand-written weights — so retraining silently had **zero** effect on the
device, and running the script from the repo root left a second header with the
same name shadowing the real one.  It also selected 5 features (adding `hop`)
when origin samples were scarce, which does not match the firmware's 4-input
forward pass.

This version:
  * writes the real header (atomically, path relative to the repo, not the CWD),
  * always emits the firmware ABI: raw[4] = [rssi, src, from, node] in order,
  * emits std = 0 for features that were constant while logging, so the firmware
    zeroes them instead of amplifying them to ±1e6 on a different node id,
  * refuses to ship a model that cannot separate relay from no-relay.
-----------------------------------------------------------------------------
"""
import os
import sys

import numpy as np
import pandas as pd

# --- firmware ABI: mlPredictRelay() takes exactly these, in this order --------
FEATURES = ["rssi", "src", "from", "node"]

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER_PATH = os.path.join(REPO, "include", "rssi_classifier.h")

# decision codes streamed by the firmware (RELAY_STATUS_* in main.cpp)
DEC_RELAY = (1, 2)  # QUEUED / SENT   → this node relayed
DEC_NO_RELAY = (4,)  # NO-RELAY        → suppressed (source close)


def load_and_prepare(csv_path):
    """Load the RSSI log and label each row with the relay decision taken."""
    df = pd.read_csv(csv_path)
    df.columns = [c.strip() for c in df.columns]

    keep = DEC_RELAY + DEC_NO_RELAY
    # Origin packets only (hop == 0): "should I be the one to relay this?"
    origin = df[(df["hop"] == 0) & (df["decision"].isin(keep))].copy()
    origin["should_relay"] = origin["decision"].isin(DEC_RELAY).astype(int)

    print("=== Dataset ===")
    print(f"  rows in log            : {len(df)}")
    print(f"  origin rows (hop=0)    : {len(origin)}")
    if len(origin):
        pos = int(origin["should_relay"].sum())
        print(f"    relay / no-relay     : {pos} / {len(origin) - pos}")
        print(f"  rssi range             : {df['rssi'].min()} .. {df['rssi'].max()} dBm")
    return df, origin


def normalization(X):
    """Per-feature mean/std. std = 0 marks a feature that never varied."""
    mean = X.mean(axis=0)
    std = X.std(axis=0)
    degenerate = std < 1e-6
    if degenerate.any():
        names = [FEATURES[i] for i in np.flatnonzero(degenerate)]
        print(f"  ! constant while logging, will be ignored by the model: {names}")
    std_out = np.where(degenerate, 0.0, std)  # 0 → firmware forces norm = 0
    return mean, std_out


def apply_norm(X, mean, std):
    safe = np.where(std < 1e-6, 1.0, std)
    Z = (X - mean) / safe
    return np.where(std < 1e-6, 0.0, Z)  # same rule as normalizeInput()


def train_mlp(X, y, epochs=4000, lr=0.02, seed=0):
    """Dense(4→8,ReLU) → Dense(8→4,ReLU) → Dense(4→1,sigmoid), Adam, BCE."""
    rng = np.random.default_rng(seed)
    n_in = X.shape[1]
    # He init for the ReLU layers
    W1 = rng.normal(0, np.sqrt(2.0 / n_in), (n_in, 8))
    b1 = np.zeros(8)
    W2 = rng.normal(0, np.sqrt(2.0 / 8), (8, 4))
    b2 = np.zeros(4)
    W3 = rng.normal(0, np.sqrt(2.0 / 4), (4, 1))
    b3 = np.zeros(1)
    params = [W1, b1, W2, b2, W3, b3]
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]

    # class weights — the log is usually dominated by one decision
    pos = max(float(y.sum()), 1.0)
    neg = max(float(len(y) - y.sum()), 1.0)
    w_pos, w_neg = len(y) / (2 * pos), len(y) / (2 * neg)
    sw = np.where(y == 1, w_pos, w_neg).reshape(-1, 1)
    Y = y.reshape(-1, 1).astype(float)

    for step in range(1, epochs + 1):
        z1 = X @ W1 + b1
        a1 = np.maximum(z1, 0)
        z2 = a1 @ W2 + b2
        a2 = np.maximum(z2, 0)
        z3 = a2 @ W3 + b3
        p = 1.0 / (1.0 + np.exp(-z3))

        dz3 = sw * (p - Y) / len(X)
        gW3, gb3 = a2.T @ dz3, dz3.sum(axis=0)
        da2 = dz3 @ W3.T
        dz2 = da2 * (z2 > 0)
        gW2, gb2 = a1.T @ dz2, dz2.sum(axis=0)
        da1 = dz2 @ W2.T
        dz1 = da1 * (z1 > 0)
        gW1, gb1 = X.T @ dz1, dz1.sum(axis=0)

        for i, g in enumerate([gW1, gb1, gW2, gb2, gW3, gb3]):
            m[i] = 0.9 * m[i] + 0.1 * g
            v[i] = 0.999 * v[i] + 0.001 * (g * g)
            mh = m[i] / (1 - 0.9 ** step)
            vh = v[i] / (1 - 0.999 ** step)
            params[i] -= lr * mh / (np.sqrt(vh) + 1e-8)
        W1, b1, W2, b2, W3, b3 = params

    return params


def predict(params, X):
    W1, b1, W2, b2, W3, b3 = params
    a1 = np.maximum(X @ W1 + b1, 0)
    a2 = np.maximum(a1 @ W2 + b2, 0)
    return (1.0 / (1.0 + np.exp(-(a2 @ W3 + b3)))).ravel()


def evaluate(params, Xn, y, mean, std):
    """Accuracy plus the two sanity gates that caught the previous bad model."""
    p = predict(params, Xn)
    acc = ((p >= 0.5).astype(int) == y).mean()
    print("\n=== Fit ===")
    print(f"  accuracy (train)       : {acc * 100:.1f}%")
    print(f"  probability range      : {p.min():.3f} .. {p.max():.3f}")

    ok = True
    if p.min() >= 0.5 or p.max() < 0.5:
        print("  ✗ every sample falls on ONE side of 0.5 — the model decides nothing.")
        ok = False

    # Suppression is only useful if a strong origin is *less* likely to be relayed.
    rssi = Xn[:, 0] * (std[0] if std[0] >= 1e-6 else 1.0) + mean[0]
    if len(np.unique(rssi)) > 1:
        strong, weak = rssi >= np.median(rssi), rssi < np.median(rssi)
        if strong.any() and weak.any():
            hi, lo = p[strong].mean(), p[weak].mean()
            print(f"  mean prob strong / weak: {hi:.3f} / {lo:.3f}")
            if hi > lo:
                print("  ✗ strong signals are relayed MORE than weak ones (inverted).")
                ok = False
    if ok:
        print("  ✓ sanity gates passed")
    return ok


def fmt(arr, per_line=4):
    vals = ["{:+.8f}f".format(v) for v in np.ravel(arr)]
    lines, indent = [], "  "
    for i in range(0, len(vals), per_line):
        lines.append(indent + ", ".join(vals[i:i + per_line]))
    return ",\n".join(lines)


def emit_header(params, mean, std, path, meta):
    W1, b1, W2, b2, W3, b3 = params
    src = f"""#ifndef RSSI_CLASSIFIER_H
#define RSSI_CLASSIFIER_H

#include <Arduino.h>
#include <math.h>

// ============================================================
// Lightweight ML inference for RSSI-based relay decision
// Architecture: Dense(4->8, ReLU) -> Dense(8->4, ReLU) -> Dense(4->1, Sigmoid)
//
// Input:  [rssi, src, from, node]  (raw values)
// Output: probability (0.0-1.0), >=0.5 = should relay
//
// GENERATED by tools/train_model.py -- do not edit by hand.
//   dataset : {meta['csv']}
//   samples : {meta['n']} (relay {meta['pos']} / no-relay {meta['neg']})
//   accuracy: {meta['acc'] * 100:.1f}% on the training set
// ============================================================

// ---- Normalization constants (from training data) ----
// A std of 0 means the feature never varied while logging, so it carries no
// information; normalizeInput() forces it to 0 instead of amplifying the
// difference (a 1e-6 floor turned a one-off node id into +-1e6 and saturated
// every ReLU).
static const float ML_MEAN[4] = {{
{fmt(mean)}
}};
static const float ML_STD[4]  = {{
{fmt(std)}
}};

// ---- Layer 1: Dense(4->8, ReLU) ----  W1 row-major [4][8]
static const float W1[32] = {{
{fmt(W1)}
}};
static const float b1[8] = {{
{fmt(b1)}
}};

// ---- Layer 2: Dense(8->4, ReLU) ----  W2 row-major [8][4]
static const float W2[32] = {{
{fmt(W2)}
}};
static const float b2[4] = {{
{fmt(b2)}
}};

// ---- Layer 3: Dense(4->1, Sigmoid) ----
static const float W3[4] = {{
{fmt(W3)}
}};
static const float b3 = {float(np.ravel(b3)[0]):+.8f}f;

// ============================================================
// normalizeInput — z-score, with degenerate features zeroed
// ============================================================
static inline void normalizeInput(float raw[4], float norm[4]) {{
    for (int i = 0; i < 4; i++) {{
        if (ML_STD[i] < 1e-6f) {{ norm[i] = 0.0f; continue; }}  // constant in training
        norm[i] = (raw[i] - ML_MEAN[i]) / ML_STD[i];
    }}
}}

// ============================================================
// mlPredictRelay — full forward pass, returns probability 0.0-1.0
// ============================================================
static inline float mlPredictRelay(const float raw[4]) {{
    float x[4];
    normalizeInput((float*)raw, x);

    float a1[8];
    for (int j = 0; j < 8; j++) {{
        float z = b1[j];
        for (int i = 0; i < 4; i++) z += x[i] * W1[i * 8 + j];
        a1[j] = (z > 0) ? z : 0.0f;
    }}

    float a2[4];
    for (int j = 0; j < 4; j++) {{
        float z = b2[j];
        for (int i = 0; i < 8; i++) z += a1[i] * W2[i * 4 + j];
        a2[j] = (z > 0) ? z : 0.0f;
    }}

    float z3 = b3;
    for (int i = 0; i < 4; i++) z3 += a2[i] * W3[i];
    return 1.0f / (1.0f + expf(-z3));
}}

// ============================================================
// Convenience: predictRelay — true when prob >= 0.5
// ============================================================
static inline bool predictRelay(int16_t rssi, uint8_t src, uint8_t rhFrom, uint8_t nodeId) {{
    float raw[4] = {{ (float)rssi, (float)src, (float)rhFrom, (float)nodeId }};
    return mlPredictRelay(raw) >= 0.5f;
}}

#endif // RSSI_CLASSIFIER_H
"""
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(src)
    os.replace(tmp, path)  # atomic: never leave a half-written header behind
    print(f"\nwrote {path}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 tools/train_model.py <rssi_log.csv>")
        sys.exit(1)
    csv_path = sys.argv[1]
    if not os.path.exists(csv_path):
        print(f"File not found: {csv_path}")
        print("Collect data first: python3 tools/rssi_logger.py /dev/tty.usbmodemXXXX")
        sys.exit(1)

    df, origin = load_and_prepare(csv_path)
    if len(origin) < 10:
        print(f"\nNeed >=10 origin rows with a relay/no-relay decision, got {len(origin)}.")
        print("Fly/deploy the nodes longer, or lower the TX interval, then retrain.")
        sys.exit(2)

    X = origin[FEATURES].values.astype(float)
    y = origin["should_relay"].values.astype(int)
    mean, std = normalization(X)
    Xn = apply_norm(X, mean, std)

    params = train_mlp(Xn, y)
    ok = evaluate(params, Xn, y, mean, std)
    acc = ((predict(params, Xn) >= 0.5).astype(int) == y).mean()

    if not ok:
        print("\nNOT writing the header — this model would decide nothing useful.")
        print("Collect more varied data (different distances/times) and retrain.")
        sys.exit(3)

    emit_header(
        params, mean, std, HEADER_PATH,
        {"csv": os.path.basename(csv_path), "n": len(y),
         "pos": int(y.sum()), "neg": int(len(y) - y.sum()), "acc": acc},
    )
    print("Rebuild the firmware to pick it up: pio run")
    print("ML mode is OFF by default in main.cpp — flip mlMode (or the serial")
    print("toggle) once you trust these numbers.")


if __name__ == "__main__":
    main()
