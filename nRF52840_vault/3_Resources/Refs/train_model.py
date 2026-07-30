#!/usr/bin/env python3
"""
train_model.py — Train a TinyML classifier from RSSI log data
ใช้งาน: python3 train_model.py rssi_log.csv

Output: rssi_classifier.h (TFLite model for embedding in firmware)

Requires: pip install tensorflow pandas numpy scikit-learn
"""
import pandas as pd
import numpy as np
import sys
import os

def load_and_prepare(csv_path):
    """Load RSSI log CSV and prepare features/labels"""
    df = pd.read_csv(csv_path)
    df['flags_dec'] = df['flags'].apply(lambda x: int(str(x), 16) if str(x) != '0' else 0)

    # --- Key insight: use decision column, not flags ---
    # decision=1 (QUEUED) or 2 (SENT) = "this node decided to relay"
    # decision=4 (NO-RELAY) = "this node decided NOT to relay" (RSSI ≥ threshold)
    # decision=5 (DROP-DUP) = cancel-on-heard (relay was cancelled)
    # decision=8 (DROP-CRC) = bad signal

    # Strategy A: Train on origin packets (hop=0) — "should I relay?"
    df_origin = df[(df['hop'] == 0) & (df['decision'].isin([1, 2, 4]))].copy()
    df_origin['should_relay'] = df_origin['decision'].apply(lambda d: 1 if d in [1, 2] else 0)

    # Strategy B: Full dataset — "is this packet being relayed?"
    df['should_relay'] = ((df['flags_dec'] & 0x40) != 0).astype(int)

    print(f"\n=== Dataset Summary ===")
    print(f"  Total samples: {len(df)}")
    print(f"  Origin (hop=0) with clear decision: {len(df_origin)}")
    if len(df_origin) > 0:
        print(f"    → should relay: {df_origin['should_relay'].sum()}")
        print(f"    → no relay:  {(1-df_origin['should_relay']).sum()}")
    print(f"  All samples: relayed={df['should_relay'].sum()}, not={(~df['should_relay'].astype(bool)).sum()}")
    print(f"  RSSI range: {df['rssi'].min()} to {df['rssi'].max()} dBm")

    return df, df_origin

def train_decision_tree(X, y, feature_names):
    """Train a simple decision tree (exportable as if-else chain)"""
    from sklearn.tree import DecisionTreeClassifier, export_text

    clf = DecisionTreeClassifier(max_depth=3, min_samples_leaf=5)
    clf.fit(X, y)

    rules = export_text(clf, feature_names=feature_names)
    print("\nDecision Tree Rules:")
    print(rules)
    return clf

def train_tflite(X, y):
    """Train a small neural network and export to TFLite"""
    try:
        import tensorflow as tf
    except ImportError:
        print("TensorFlow not installed. Install: pip install tensorflow")
        return None

    # Normalize features
    mean = X.mean(axis=0)
    std = X.std(axis=0) + 1e-8
    X_norm = (X - mean) / std

    # Build tiny model (5 features: rssi, hop, src, from, node)
    model = tf.keras.Sequential([
        tf.keras.layers.Dense(8, activation='relu', input_shape=(X.shape[1],)),
        tf.keras.layers.Dense(4, activation='relu'),
        tf.keras.layers.Dense(1, activation='sigmoid'),
    ])
    model.compile(optimizer='adam', loss='binary_crossentropy', metrics=['accuracy'])

    model.fit(X_norm, y, epochs=20, batch_size=32, verbose=1)

    # Convert to TFLite
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    tflite_model = converter.convert()

    # Save as C header
    with open('rssi_classifier.h', 'wb') as f:
        f.write(b'#ifndef RSSI_CLASSIFIER_H\n#define RSSI_CLASSIFIER_H\n\n')
        f.write(b'// Auto-generated TFLite model for RSSI classification\n')
        f.write(b'// Input: [rssi, hop, src] normalized\n')
        f.write(b'// Output: probability (0-1) of should_relay\n\n')
        f.write(b'const unsigned char rssi_classifier_tflite[] = {\n')
        for i, b in enumerate(tflite_model):
            if i % 12 == 0:
                f.write(b'  ')
            f.write(f'0x{b:02x}, '.encode())
            if i % 12 == 11:
                f.write(b'\n')
        f.write(b'\n};\n')
        f.write(f'const unsigned int rssi_classifier_tflite_len = {len(tflite_model)};\n'.encode())
        f.write(b'\n#endif\n')

    print(f"\nTFLite model saved: rssi_classifier.h ({len(tflite_model)} bytes)")
    print(f"  Normalization: mean={mean}, std={std}")
    return model

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 train_model.py <rssi_log.csv>")
        sys.exit(1)

    csv_path = sys.argv[1]
    if not os.path.exists(csv_path):
        print(f"File not found: {csv_path}")
        print("First collect data: python3 rssi_logger.py /dev/tty.usbmodemXXXX")
        sys.exit(1)

    df, df_origin = load_and_prepare(csv_path)

    # Train on origin packets (hop=0) — "should I relay?"
    if len(df_origin) >= 5:
        features = ['rssi', 'src', 'from', 'node']
        X = df_origin[features].values
        y = df_origin['should_relay'].values
        print(f"\n--- Decision Tree: Origin packets (should relay?) ---")
        train_decision_tree(X, y, features)
    else:
        print(f"\n  Not enough origin samples ({len(df_origin)}), using full dataset")
        features = ['rssi', 'hop', 'src', 'from', 'node']
        X = df[features].values
        y = df['should_relay'].values
        print(f"\n--- Decision Tree: Full dataset ---")
        train_decision_tree(X, y, features)

    print("\n--- TFLite Model (requires TensorFlow) ---")
    train_tflite(X, y)

if __name__ == "__main__":
    main()