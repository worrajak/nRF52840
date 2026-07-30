#ifndef RSSI_CLASSIFIER_H
#define RSSI_CLASSIFIER_H

#include <Arduino.h>
#include <math.h>

// ============================================================
// Lightweight ML inference for RSSI-based relay decision
// Architecture: Dense(4→8, ReLU) → Dense(8→4, ReLU) → Dense(4→1, Sigmoid)
// Trained from rssi_log.csv (origin packets, hop=0)
//
// Input:  [rssi, src, from, node]  (raw values)
// Output: probability (0.0–1.0), ≥0.5 = should relay
// ============================================================

// ---- Normalization constants (from training data) ----
static const float ML_MEAN[4] = { -59.068f, 104.727f, 104.727f, 117.0f };
static const float ML_STD[4]  = {  21.694f,  23.077f,  23.077f,   1e-8f };

// ---- Layer 1: Dense(4→8, ReLU) ----
// W1 shape: [4, 8] (row-major: 4 inputs × 8 outputs)
static const float W1[32] = {
  -0.13619086f, -0.31385308f,  0.67192084f, -0.43216357f,
   0.09795889f, -0.27269974f, -0.06096302f,  0.45533386f,
   0.18659733f, -0.58944178f,  0.14347281f,  0.62927997f,
   0.26384997f,  0.61081761f, -0.03331681f, -0.04885859f,
  -0.00284265f,  0.02039689f, -0.51853991f,  0.09756219f,
   0.12199907f, -0.57982564f,  0.03818673f,  0.10747969f,
  -0.44375402f,  0.70350772f, -0.66837788f,  0.23070103f,
   0.53955019f, -0.64105749f,  0.35084432f, -0.64857543f
};
// b1 shape: [8]
static const float b1[8] = {
   0.01214660f, -0.01642820f,  0.01641094f,  0.01643868f,
  -0.03806170f, -0.02019662f, -0.01634822f, -0.01644469f
};

// ---- Layer 2: Dense(8→4, ReLU) ----
// W2 shape: [8, 4] (row-major: 8 inputs × 4 outputs)
static const float W2[32] = {
   0.32611993f, -0.02235328f,  0.44014797f,  0.20155215f,
   0.38566884f,  0.55935270f,  0.46501735f,  0.35914892f,
   0.27994987f,  0.69937670f,  0.58710039f,  0.38023728f,
  -0.63995409f,  0.61574310f,  0.37832668f,  0.49998504f,
  -0.02753746f, -0.39898324f,  0.00471794f,  0.29998428f,
   0.46257436f,  0.28512731f,  0.22773875f, -0.23278648f,
  -0.22939868f, -0.00196791f,  0.03472560f,  0.02520919f,
  -0.63325953f,  0.27651492f, -0.02911634f, -0.27565268f
};
// b2 shape: [4]
static const float b2[4] = {
  -0.02877419f, -0.03962565f,  0.03991653f, -0.03864937f
};

// ---- Layer 3: Dense(4→1, Sigmoid) ----
// W3 shape: [1, 4] → treat as [4] for dot product
static const float W3[4] = {
   0.03394548f,  1.04632807f, -0.81523168f,  0.15303713f
};
// b3 shape: [1]
static const float b3 = -0.03962724f;

// ============================================================
// normalizeInput — apply z-score normalization
// ============================================================
static inline void normalizeInput(float raw[4], float norm[4]) {
    for (int i = 0; i < 4; i++) {
        // Guard against zero std (e.g., node is constant)
        float s = (ML_STD[i] < 1e-6f) ? 1e-6f : ML_STD[i];
        norm[i] = (raw[i] - ML_MEAN[i]) / s;
    }
}

// ============================================================
// mlPredictRelay — run full forward pass
// Returns: probability (0.0–1.0)
// Input raw[4] = {rssi, src, from, node}  (as floats)
// ============================================================
static inline float mlPredictRelay(const float raw[4]) {
    float x[4];
    normalizeInput((float*)raw, x);

    // ---- Layer 1: Dense(4→8, ReLU) ----
    float a1[8];
    for (int j = 0; j < 8; j++) {
        float z = b1[j];
        for (int i = 0; i < 4; i++) {
            z += x[i] * W1[i * 8 + j];  // W1[i][j]
        }
        a1[j] = (z > 0) ? z : 0.0f;  // ReLU
    }

    // ---- Layer 2: Dense(8→4, ReLU) ----
    float a2[4];
    for (int j = 0; j < 4; j++) {
        float z = b2[j];
        for (int i = 0; i < 8; i++) {
            z += a1[i] * W2[i * 4 + j];  // W2[i][j]
        }
        a2[j] = (z > 0) ? z : 0.0f;  // ReLU
    }

    // ---- Layer 3: Dense(4→1, Sigmoid) ----
    float z3 = b3;
    for (int i = 0; i < 4; i++) {
        z3 += a2[i] * W3[i];
    }
    // Sigmoid: 1 / (1 + exp(-z))
    float prob = 1.0f / (1.0f + expf(-z3));

    return prob;
}

// ============================================================
// Convenience: predictRelay — returns bool (≥0.5 = should relay)
// ============================================================
static inline bool predictRelay(int16_t rssi, uint8_t src, uint8_t rhFrom, uint8_t nodeId) {
    float raw[4] = { (float)rssi, (float)src, (float)rhFrom, (float)nodeId };
    float prob = mlPredictRelay(raw);
    return (prob >= 0.5f);
}

#endif // RSSI_CLASSIFIER_H