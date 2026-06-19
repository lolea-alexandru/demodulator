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
    return hi->x - lo->x;            // max x - min x
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
JNIEXPORT jdouble JNICALL
Java_com_example_demodulator_OpenCVUtils_otsuThreshold(JNIEnv *env, jobject thiz, jobject ogBitmap,
                                                       jobject led) {
    // --- read the LED's bounding box ---
    jclass cls = env->GetObjectClass(led);   // class from the instance, no hardcoded name
    cv::Rect box(
            env->GetIntField(led, env->GetFieldID(cls, "x", "I")),
            env->GetIntField(led, env->GetFieldID(cls, "y", "I")),
            env->GetIntField(led, env->GetFieldID(cls, "width", "I")),
            env->GetIntField(led, env->GetFieldID(cls, "height", "I")));

    // --- inspect the bitmap ---
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, ogBitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
        info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
        return -1.0;                          // sentinel: Otsu is always in [0,255]

    box &= cv::Rect(0, 0, info.width, info.height);  // clamp before cropping
    if (box.area() == 0) return -1.0;

    // --- lock, convert only the ROI, unlock ---
    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, ogBitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
        return -1.0;

    cv::Mat rgba(info.height, info.width, CV_8UC4, pixels, info.stride);  // wraps, no copy
    cv::Mat grayRoi;
    cv::cvtColor(rgba(box), grayRoi, cv::COLOR_RGBA2GRAY);    // converts just the region
    AndroidBitmap_unlockPixels(env, ogBitmap);               // grayRoi owns its data now

    // --- Otsu ---
    cv::Mat binary;
    return cv::threshold(grayRoi, binary, 0, 255,
                         cv::THRESH_BINARY | cv::THRESH_OTSU);  // returns the threshold
}
extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_demodulator_OpenCVUtils_demodulate(JNIEnv *env, jobject thiz, jobject og_bitmap,
                                                    jobject led, jdouble otsu_threshold) {
    // TODO: implement demodulate()
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