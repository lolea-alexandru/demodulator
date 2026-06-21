#include <jni.h>
#include <string>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <android/bitmap.h>
#include <android/log.h>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <opencv2/core.hpp>

// DEBUG LOGS
#define LOG_TAG "OccNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

int averageLedGap(std::vector<cv::Rect> leds) {
    constexpr int ROWS = 6;
    constexpr int COLS = 4;
    LOGI("The avg gap between LEDs is: %zu", leds.size());
    CV_Assert(leds.size() == ROWS * COLS);

    // 1. Sort by x so columns group together, leftmost column first.
    std::sort(leds.begin(), leds.end(),
              [](const cv::Rect& a, const cv::Rect& b) { return a.x < b.x; });

    // 2. Sort each column (a chunk of ROWS) top-to-bottom by y.
    for (int c = 0; c < COLS; ++c)
        std::sort(leds.begin() + c * ROWS, leds.begin() + (c + 1) * ROWS,
                  [](const cv::Rect& a, const cv::Rect& b) { return a.y < b.y; });

    // leds is now column-major: index i -> column i/ROWS, row i%ROWS.
    // Horizontal neighbour of i is the same row in the next column: i + ROWS.
    std::vector<int> gaps;
    gaps.reserve(ROWS * (COLS - 1));                 // 18
    for (int i = 0; i < ROWS * (COLS - 1); ++i) {
        int gap = leds[i + ROWS].x - (leds[i].x + leds[i].width);  // edge-to-edge
        gaps.push_back(gap);
    }

    double avg = std::accumulate(gaps.begin(), gaps.end(), 0.0) / gaps.size();
    return static_cast<int>(std::ceil(avg));
}

// --- pure C++ computation: widest horizontal span of x ------------------
int maxXSpan(const std::vector<cv::Rect>& leds) {
    if (leds.empty()) return 0;
    auto [lo, hi] = std::minmax_element(
            leds.begin(), leds.end(),
            [](const cv::Rect& a, const cv::Rect& b) { return a.x < b.x; });
    return (hi->x + hi->width) - lo->x;            // max x - min x
}

// Otsu thresholding
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_example_demodulator_OpenCVUtils_findLEDBounds(
        JNIEnv* env, jobject /* this */,
        jobject input, jobject output) {

    // Create the framework for sending the code to Kotlin
    jclass cls = env->FindClass("com/example/demodulator/LED");
    jmethodID constructor = env->GetMethodID(cls, "<init>", "(IIII)V");

    /* ============================= OTSU THRESHOLDING ============================= */
    AndroidBitmapInfo inInfo, outInfo;
    if (AndroidBitmap_getInfo(env, input,  &inInfo)  != ANDROID_BITMAP_RESULT_SUCCESS ||
        AndroidBitmap_getInfo(env, output, &outInfo) != ANDROID_BITMAP_RESULT_SUCCESS) {
        return env->NewObjectArray(0, cls, nullptr);
    }

    void* inPixels  = nullptr;
    void* outPixels = nullptr;
    AndroidBitmap_lockPixels(env, input,  &inPixels);
    AndroidBitmap_lockPixels(env, output, &outPixels);

    cv::Mat src(inInfo.height,  inInfo.width,  CV_8UC4, inPixels,  inInfo.stride);
    cv::Mat dst(outInfo.height, outInfo.width, CV_8UC4, outPixels, outInfo.stride);

    LOGI("Matrix width = %.1u", inInfo.width);
    LOGI("Matrix height = %.1u", inInfo.height);

    cv::Mat gray, binary;
    cv::cvtColor(src, gray, cv::COLOR_RGBA2GRAY);
    double t = cv::threshold(gray, binary, 0, 255,
                             cv::THRESH_BINARY | cv::THRESH_OTSU);
    LOGI("Otsu threshold = %.1f", t);
    cv::cvtColor(binary, dst, cv::COLOR_GRAY2RGBA);   // fills output's locked buffer

    /* ============================= MORPHOLOGICALLY CLOSE ============================= */
    // Create a stamp and apply it to every pixel in 2 steps: dilation & erosion
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(175, 150));
    cv::Mat closed;
    cv::morphologyEx(binary, closed, cv::MORPH_CLOSE, kernel);

    cv::cvtColor(closed, dst, cv::COLOR_GRAY2RGBA);

    /* ============================= FLOOD-FILL ============================= */
    // Invert colors to make the gaps white and then add them to the original picture
    cv::Mat floodFilled = closed.clone();
    cv::floodFill(floodFilled, cv::Point(0, 0), cv::Scalar(255));

    cv::Mat holes;
    cv::bitwise_not(floodFilled, holes);

    cv::Mat filled = closed | holes;
    cv::cvtColor(closed, dst, cv::COLOR_GRAY2RGBA);

    /* ============================= CONVEX HULL ============================= */
    cv::Mat labels, stats, centroids;
    int numLabels = cv::connectedComponentsWithStats(
            binary, labels, stats, centroids, 8, CV_32S);

    // Return the colors to the dst
    src.copyTo(dst);

    // For debug
    int idx = 1;
    std::vector<cv::Rect> squareBounds;
    // Start from 1; 0 is the background which we are not interested in
    for (int i = 1; i < numLabels; i++) {
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < 100) continue;                       // drop tiny noise blobs (tune the 50)

        // Create the box around
        int x = stats.at<int>(i, cv::CC_STAT_LEFT);
        int y = stats.at<int>(i, cv::CC_STAT_TOP);
        int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
        int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        cv::Rect bbox(x, y, w, h);
        squareBounds.push_back(bbox);

        double cx = centroids.at<double>(i, 0);         // centroid x
        double cy = centroids.at<double>(i, 1);         // centroid y

        LOGI("blob %d: bbox=(%d,%d,%d,%d) area=%d centroid=(%.1f,%.1f)",
             i, x, y, w, h, area, cx, cy);

        cv::rectangle(dst, bbox, cv::Scalar(0, idx*10, 0, 255), 2);
        idx++;
    }

    auto n = static_cast<jsize>(squareBounds.size());
    jobjectArray ledBounds = env->NewObjectArray(n, cls, nullptr);


    // Add the rectangles to the ledBounds vector
    for(int i=0; i<squareBounds.size(); i++) {
        const cv::Rect& bbox = squareBounds[i];
        // Create the bound object
        jobject bound = env->NewObject(cls, constructor, bbox.x, bbox.y, bbox.width, bbox.height);

        // Add the new element
        env->SetObjectArrayElement(ledBounds, i, bound);

        // Delete the memory ref => the object array holds its own
        env->DeleteLocalRef(bound);
    }

    cv::Mat bigLabels, bigStats, bigCentroids;
    int bigNumLabels = cv::connectedComponentsWithStats(
            filled, bigLabels, bigStats, bigCentroids, 8, CV_32S);

    for(int i=1; i<bigNumLabels; i++) {
        int area = bigStats.at<int>(i, cv::CC_STAT_AREA);
        if (area < 100) continue;                       // drop tiny noise blobs (tune the 50)

        // Create the box around
        int x = bigStats.at<int>(i, cv::CC_STAT_LEFT);
        int y = bigStats.at<int>(i, cv::CC_STAT_TOP);
        int w = bigStats.at<int>(i, cv::CC_STAT_WIDTH);
        int h = bigStats.at<int>(i, cv::CC_STAT_HEIGHT);
        cv::Rect bbox(x, y, w, h);

        double cx = bigCentroids.at<double>(i, 0);         // centroid x
        double cy = bigCentroids.at<double>(i, 1);         // centroid y

        LOGI("blob %d: bbox=(%d,%d,%d,%d) area=%d centroid=(%.1f,%.1f)",
             i, x, y, w, h, area, cx, cy);

        cv::rectangle(dst, bbox, cv::Scalar(255, 0, 0, 255), 5);
    }

    LOGI("The avg gap between LEDs is: %d", averageLedGap(squareBounds));

    AndroidBitmap_unlockPixels(env, output);
    AndroidBitmap_unlockPixels(env, input);


    return ledBounds;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_demodulator_OpenCVUtils_findColumnGap(JNIEnv *env, jobject thiz,
                                                       jobjectArray leds) {
    const jsize n = env->GetArrayLength(leds);

    // Resolve the LED class and its field IDs once, before the loop.
    // Kotlin data-class `val` backing fields are private, but JNI field
    // access bypasses Java access control, so reading them by name works.
    jclass ledCls = env->FindClass("com/example/demodulator/LED");
    jfieldID fX = env->GetFieldID(ledCls, "x", "I");
    jfieldID fY = env->GetFieldID(ledCls, "y", "I");
    jfieldID fW = env->GetFieldID(ledCls, "width", "I");
    jfieldID fH = env->GetFieldID(ledCls, "height", "I");

    std::vector<cv::Rect> rects;
    rects.reserve(n);

    for (jsize i = 0; i < n; ++i) {
        jobject led = env->GetObjectArrayElement(leds, i);
        const jint x = env->GetIntField(led, fX);
        const jint y = env->GetIntField(led, fY);
        const jint w = env->GetIntField(led, fW);
        const jint h = env->GetIntField(led, fH);
        rects.emplace_back(x, y, w, h);     // cv::Rect(x, y, width, height)
        env->DeleteLocalRef(led);           // don't accumulate 24 local refs
    }

    env->DeleteLocalRef(ledCls);

    return static_cast<jint>(averageLedGap(std::move(rects)));
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_demodulator_OpenCVUtils_findMaxSpan(JNIEnv *env, jobject thiz, jobjectArray leds) {
    const jsize n = env->GetArrayLength(leds);

    jclass ledCls = env->FindClass("com/example/demodulator/LED");
    jfieldID fX = env->GetFieldID(ledCls, "x", "I");
    jfieldID fY = env->GetFieldID(ledCls, "y", "I");
    jfieldID fW = env->GetFieldID(ledCls, "width", "I");
    jfieldID fH = env->GetFieldID(ledCls, "height", "I");

    std::vector<cv::Rect> rects;
    rects.reserve(n);
    for (jsize i = 0; i < n; ++i) {
        jobject led = env->GetObjectArrayElement(leds, i);
        const jint x = env->GetIntField(led, fX);
        const jint y = env->GetIntField(led, fY);
        const jint w = env->GetIntField(led, fW);
        const jint h = env->GetIntField(led, fH);
        rects.emplace_back(x, y, w, h);
        env->DeleteLocalRef(led);
    }
    env->DeleteLocalRef(ledCls);

    return static_cast<jint>(maxXSpan(rects));   // pure C++ compute
}

void sortLedsColumnMajor(std::vector<cv::Rect>& leds) {
    constexpr int ROWS = 6;                          // LEDs per column (chunk size)

    // 1. by x: columns group together, leftmost first
    std::sort(leds.begin(), leds.end(),
              [](const cv::Rect& a, const cv::Rect& b) { return a.x < b.x; });

    // 2. within each chunk of ROWS (one column): top-to-bottom by y
    for (size_t start = 0; start < leds.size(); start += ROWS) {
        auto end = leds.begin() + std::min(start + ROWS, leds.size());
        std::sort(leds.begin() + start, end,
                  [](const cv::Rect& a, const cv::Rect& b) { return a.y < b.y; });
    }
}

// Minimum Hamming distance between `decoded` and the cyclic `truth` period,
// over all starting offsets 0 .. truth.size()-1.
double minHammingDistance(const std::string& decoded) {
    std::string truth = "1111111101101010011111111101101010011111111101101010101111111101101010101111111110011010";
    if (decoded.find_first_not_of('0') == std::string::npos)
        return 0;

    const int P = static_cast<int>(truth.size());
    const int L = static_cast<int>(decoded.size());
    if (P == 0) return L;            // no truth to compare against

    int best = L;                    // worst case: every position mismatches
    for (int k = 0; k < P; ++k) {    // try every cyclic offset into the truth
        int dist = 0;
        for (int i = 0; i < L; ++i)
            if (decoded[i] != truth[(k + i) % P])   // % P wraps the period
                ++dist;
        best = std::min(best, dist);
    }
    return best;
}


std::string demodulate(const cv::Mat& gray, const cv::Rect& led,
                       double otsuThreshold, int columnsPerSymbol) {
    CV_Assert(columnsPerSymbol > 0);

    // Step 1: take the LED region (clamped to the image; this is a view).
    cv::Rect bounds = led & cv::Rect(0, 0, gray.cols, gray.rows);
    cv::Mat roi = gray(bounds);

    // Step 2: walk every column right -> left (temporal order of the scan) and
    // mark it ON/OFF by comparing the column mean to the Otsu threshold.
    std::vector<bool> columnOn;
    columnOn.reserve(roi.cols);
    for (int c = roi.cols - 1; c >= 0; --c) {
        double colMean = cv::mean(roi.col(c))[0];
//        LOGI("COL_MEAN %d: %.1f , %.1f\n", c , colMean, otsuThreshold);

        columnOn.push_back(colMean > std::max(otsuThreshold, 10.0));
    }
    LOGI("=================================== END OF MODULATED LED ===================================");


    // Step 3: group columns into symbols of `columnsPerSymbol`, majority-vote
    // each group (ON if at least half its columns are ON), append '1'/'0'.
    int guard = columnsPerSymbol / 8;
    std::string bits;
    bits.reserve(columnOn.size() / columnsPerSymbol);
    for (int start = 0;
         start + columnsPerSymbol <= static_cast<int>(columnOn.size());
         start += columnsPerSymbol) {
        int onCount = 0;
        for (int k = guard; k < columnsPerSymbol - guard; ++k)
            if (columnOn[start + k]) ++onCount;
        bool symbolOn = (2 * onCount >= columnsPerSymbol);   // at least half ON
        bits += symbolOn ? '1' : '0';
    }


    // Step 4: return the decoded bit string (first char = rightmost symbol).
    return bits;
}


std::string demodulate_sliding_window(const cv::Mat& gray, const cv::Rect& led,
                                      double t, int columnsPerSymbol) {
    CV_Assert(columnsPerSymbol > 0);

    // Step 1: take the LED region (clamped; a view into gray).
    cv::Rect bounds = led & cv::Rect(0, 0, gray.cols, gray.rows);
    cv::Mat roi = gray(bounds);
    const int n = roi.cols;

    // Step 2: per-column mean, right -> left (index 0 = rightmost = earliest in time).
    std::vector<double> colMeans;
    colMeans.reserve(n);
    for (int c = n - 1; c >= 0; --c)
        colMeans.push_back(cv::mean(roi.col(c))[0]);

    // Step 3: prefix sums over the column means (the 1-D integral image) for O(1) windows.
    std::vector<double> prefix(n + 1, 0.0);
    for (int i = 0; i < n; ++i)
        prefix[i + 1] = prefix[i] + colMeans[i];

    // Step 4: decide each column against its local window mean, bumped up by t%.
    const int W = 4 * columnsPerSymbol;          // window spans several symbols
    const double factor = 1.0 + t / 100.0;
    std::vector<bool> columnOn(n);
    for (int c = 0; c < n; ++c) {
        int lo = std::max(0, c - W / 2);
        int hi = std::min(n, c + W / 2 + 1);     // exclusive; clamps the borders
        double localMean = (prefix[hi] - prefix[lo]) / (hi - lo);
        columnOn[c] = colMeans[c] > localMean * factor;   // ON if clearly above neighbourhood
    }

    // Step 5: guard-trimmed majority vote per symbol (unchanged).
    const int guard = columnsPerSymbol / 4;
    std::string bits;
    bits.reserve(n / columnsPerSymbol);
    for (int start = 0; start + columnsPerSymbol <= n; start += columnsPerSymbol) {
        int onCount = 0, total = 0;
        for (int k = guard; k < columnsPerSymbol - guard; ++k) {
            if (columnOn[start + k]) ++onCount;
            ++total;
        }
        bits += (2 * onCount >= total) ? '1' : '0';
    }

    // Step 6: return (first char = rightmost symbol).
    return bits;
}

std::vector<std::string> decodeBoard(const cv::Mat& bitmap,
                                     std::vector<cv::Rect> leds,
                                     int columnsPerSymbol) {
    sortLedsColumnMajor(leds);

    cv::Mat gray;
    cv::cvtColor(bitmap, gray, cv::COLOR_RGBA2GRAY);   // for the per-region Otsu

    std::vector<std::string> out;
    out.reserve(leds.size());
    for (const cv::Rect& led : leds) {
        cv::Rect b = led & cv::Rect(0, 0, gray.cols, gray.rows);
        cv::Mat binarized;
        double otsu = cv::threshold(gray(b), binarized, 0, 255,
                                    cv::THRESH_BINARY | cv::THRESH_OTSU);  // per-LED threshold

        LOGI("=================================== START DEMODULATION LED ===================================");

        double lo, hi;
        cv::minMaxLoc(gray(b), &lo, &hi);
//        LOGI("MINMAX: min=%.1f, max=%.1f", lo, hi);

        double t = 3.0;
        std::string decodeResult = demodulate(gray, led, otsu, columnsPerSymbol);
        int missed = minHammingDistance(decodeResult);

        LOGI("RES: %s ---------- BER: %.1f", decodeResult.c_str(), static_cast<double>(missed)/decodeResult.size());

        out.push_back(decodeResult);
//        out.push_back(demodulate_sliding_window(gray, led, t, columnsPerSymbol));
    }
    return out;
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_com_example_demodulator_OpenCVUtils_demodulateBoardColMajor(JNIEnv *env, jobject thiz, jobject bitmap,
                                                    jobjectArray ledArray, jint columnsPerSymbol) {
    // --- wrap the Android bitmap as a cv::Mat (no copy) ---
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS)
        return nullptr;
    CV_Assert(info.format == ANDROID_BITMAP_FORMAT_RGBA_8888);   // CV_8UC4 / RGBA

    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
        return nullptr;

    // pass info.stride as the Mat step: bitmap rows may be padded
    cv::Mat mat(info.height, info.width, CV_8UC4, pixels, info.stride);

    // --- convert the LED object to a cv::Rect ---
    jsize ledCount = env->GetArrayLength(ledArray);
    LOGI("LED_COUNT: %d", ledCount);
    jclass ledCls = env->FindClass("com/example/demodulator/LED");
    jfieldID fX = env->GetFieldID(ledCls, "x", "I");
    jfieldID fY = env->GetFieldID(ledCls, "y", "I");
    jfieldID fW = env->GetFieldID(ledCls, "width", "I");
    jfieldID fH = env->GetFieldID(ledCls, "height", "I");
    std::vector<cv::Rect> leds;
    leds.reserve(ledCount);
    for (jsize i = 0; i < ledCount; ++i) {
        jobject ledObj = env->GetObjectArrayElement(ledArray, i);
        leds.emplace_back(env->GetIntField(ledObj, fX),
                          env->GetIntField(ledObj, fY),
                          env->GetIntField(ledObj, fW),
                          env->GetIntField(ledObj, fH));
        env->DeleteLocalRef(ledObj);
    }
    env->DeleteLocalRef(ledCls);

    // --- run the pure C++ demodulator, then release the bitmap ---
    std::vector<std::string> decoded_board = decodeBoard(mat, leds, columnsPerSymbol);
    AndroidBitmap_unlockPixels(env, bitmap);   // demodulate copied into its own gray Mat

    // --- std::vector<std::string> -> Kotlin Array<String> ---
    jclass strCls = env->FindClass("java/lang/String");
    jobjectArray result =
            env->NewObjectArray(static_cast<jsize>(decoded_board.size()), strCls, nullptr);
    for (jsize i = 0; i < static_cast<jsize>(decoded_board.size()); ++i) {
        jstring s = env->NewStringUTF(decoded_board[i].c_str());
        env->SetObjectArrayElement(result, i, s);   // array takes its own ref
        env->DeleteLocalRef(s);                      // drop ours so they don't pile up
    }
    env->DeleteLocalRef(strCls);

    return result;
}

// Draws each LED's box onto the image in place.
void drawLedBounds(cv::Mat& image, const std::vector<cv::Rect>& leds) {
    const cv::Scalar color(0, 255, 0, 255);   // RGBA: opaque green
    const int thickness = 2;
    for (const cv::Rect& led : leds)
        cv::rectangle(image, led, color, thickness);   // OpenCV clips boxes at the edges
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_demodulator_OpenCVUtils_drawLEDBounds(JNIEnv *env, jobject thiz,
                                                       jobject bitmap, jobjectArray ledArray) {
    // --- wrap the Android bitmap as a cv::Mat (no copy; writes land in the bitmap) ---
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS)
        return;
    CV_Assert(info.format == ANDROID_BITMAP_FORMAT_RGBA_8888);   // CV_8UC4 / RGBA
    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
        return;
    cv::Mat mat(info.height, info.width, CV_8UC4, pixels, info.stride);

    // --- convert the LED array to a std::vector<cv::Rect> ---
    jsize ledCount = env->GetArrayLength(ledArray);
    jclass ledCls = env->FindClass("com/example/demodulator/LED");
    jfieldID fX = env->GetFieldID(ledCls, "x", "I");
    jfieldID fY = env->GetFieldID(ledCls, "y", "I");
    jfieldID fW = env->GetFieldID(ledCls, "width", "I");
    jfieldID fH = env->GetFieldID(ledCls, "height", "I");

    // Create the leds array
    std::vector<cv::Rect> leds;
    leds.reserve(ledCount);
    for (jsize i = 0; i < ledCount; ++i) {
        jobject ledObj = env->GetObjectArrayElement(ledArray, i);
        leds.emplace_back(env->GetIntField(ledObj, fX),
                          env->GetIntField(ledObj, fY),
                          env->GetIntField(ledObj, fW),
                          env->GetIntField(ledObj, fH));
        env->DeleteLocalRef(ledObj);
    }
    env->DeleteLocalRef(ledCls);

    // --- draw in place, then release the bitmap (writes commit on unlock) ---
    drawLedBounds(mat, leds);
    AndroidBitmap_unlockPixels(env, bitmap);
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_com_example_demodulator_OpenCVUtils_minHammingDistance(JNIEnv *env, jobject thiz,
                                                            jstring decoded_bits) {
    // pull the Java string into a std::string
    const char* chars = env->GetStringUTFChars(decoded_bits, nullptr);
    std::string decoded(chars);
    env->ReleaseStringUTFChars(decoded_bits, chars);   // release once you've copied it

    // truth is hardcoded inside minHammingDistance, so just hand it the decode
    return static_cast<jdouble>(minHammingDistance(decoded));
}