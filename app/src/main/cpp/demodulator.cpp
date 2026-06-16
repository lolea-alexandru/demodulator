#include <jni.h>
#include <string>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <android/bitmap.h>
#include <android/log.h>

// DEBUG LOGS
#define LOG_TAG "OccNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


extern "C" JNIEXPORT jstring JNICALL
Java_com_example_demodulator_MainActivity_stringFromCpp(
        JNIEnv* env,
        jobject,
        jstring name
        ) {
    const char* nameChars = env->GetStringUTFChars(name, nullptr); //get the string
    std::string result = "Hello, " + std::string(nameChars) + "!"; // Create the result
    env->ReleaseStringUTFChars(name, nameChars);

    return env->NewStringUTF(result.c_str());
}

//Basic OpenCV testing
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_demodulator_OpenCVUtils_getBitmapInfo(
        JNIEnv* env,
        jobject /* this */,
        jobject bitmap) {

    // 1. Get basic info about the Android Bitmap
    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) {
        return env->NewStringUTF("Error: Could not get Bitmap info.");
    }

    // Ensure the Bitmap is ARGB_8888 (standard Android format)
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        return env->NewStringUTF("Error: Bitmap format is not RGBA_8888.");
    }

    // 2. Lock the pixels so Android's Garbage Collector doesn't move them while we work
    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) < 0) {
        return env->NewStringUTF("Error: Could not lock Bitmap pixels.");
    }

    // 3. Wrap the raw pixel data in an OpenCV cv::Mat
    // Note: We use CV_8UC4 because Android ARGB_8888 Bitmaps have 4 channels (Red, Green, Blue, Alpha)
    cv::Mat imageMat(info.height, info.width, CV_8UC4, pixels, info.stride);

    // --- You now have a working OpenCV Matrix! ---

    // 4. Read properties directly from the OpenCV Matrix
    int matWidth = imageMat.cols;
    int matHeight = imageMat.rows;
    int matChannels = imageMat.channels();

    // Example: Read the center pixel's color values
    cv::Vec4b centerPixel = imageMat.at<cv::Vec4b>(matHeight / 2, matWidth / 2);
    int red = centerPixel[0]; // OpenCV loads RGBA as BGRA natively
    int blue = centerPixel[1];
    int green = centerPixel[2];

    // 5. Unlock the pixels to hand control back to Android
    AndroidBitmap_unlockPixels(env, bitmap);

    // 6. Format a summary string to return to Kotlin
    char result[256];
    snprintf(result, sizeof(result),
             "OpenCV Mat Properties:\nWidth: %d\nHeight: %d\nChannels: %d\nCenter RGB: (%d, %d, %d)",
             matWidth, matHeight, matChannels, red, green, blue);

    return env->NewStringUTF(result);
}

// Otsu thresholding
extern "C" JNIEXPORT void JNICALL
Java_com_example_demodulator_OpenCVUtils_otsuThreshold(
        JNIEnv* env, jobject /* this */,
        jobject input, jobject output) {

    /* ============================= OTSU THRESHOLDING ============================= */
    AndroidBitmapInfo inInfo, outInfo;
    if (AndroidBitmap_getInfo(env, input,  &inInfo)  != ANDROID_BITMAP_RESULT_SUCCESS ||
        AndroidBitmap_getInfo(env, output, &outInfo) != ANDROID_BITMAP_RESULT_SUCCESS) {
        return;
    }

    void* inPixels  = nullptr;
    void* outPixels = nullptr;
    AndroidBitmap_lockPixels(env, input,  &inPixels);
    AndroidBitmap_lockPixels(env, output, &outPixels);

    cv::Mat src(inInfo.height,  inInfo.width,  CV_8UC4, inPixels,  inInfo.stride);
    cv::Mat dst(outInfo.height, outInfo.width, CV_8UC4, outPixels, outInfo.stride);

    cv::Mat gray, binary;
    cv::cvtColor(src, gray, cv::COLOR_RGBA2GRAY);
    double t = cv::threshold(gray, binary, 0, 255,
                             cv::THRESH_BINARY | cv::THRESH_OTSU);
    LOGI("Otsu threshold = %.1f", t);
    cv::cvtColor(binary, dst, cv::COLOR_GRAY2RGBA);   // fills output's locked buffer

    /* ============================= MORPHOLOGICALLY CLOSE ============================= */
    // Create a stamp and apply it to every pixel in 2 steps: dilation & erosion
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(145, 160));
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
    cv::cvtColor(filled, dst, cv::COLOR_GRAY2RGBA);

    /* ============================= CONVEX HULL ============================= */
    cv::Mat labels, stats, centroids;
    int numLabels = cv::connectedComponentsWithStats(
            filled, labels, stats, centroids, 8, CV_32S);

    // Start from 1 => 0 is the background
    for (int i = 1; i < numLabels; i++) {
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < 50) continue;                       // drop tiny noise blobs (tune the 50)

        int x = stats.at<int>(i, cv::CC_STAT_LEFT);
        int y = stats.at<int>(i, cv::CC_STAT_TOP);
        int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
        int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        cv::Rect bbox(x, y, w, h);                      // the enclosing box

        double cx = centroids.at<double>(i, 0);         // centroid x
        double cy = centroids.at<double>(i, 1);         // centroid y

        LOGI("blob %d: bbox=(%d,%d,%d,%d) area=%d centroid=(%.1f,%.1f)",
             i, x, y, w, h, area, cx, cy);

        cv::rectangle(dst, bbox, cv::Scalar(0, 255, 0, 255), 2);
    }

    AndroidBitmap_unlockPixels(env, output);
    AndroidBitmap_unlockPixels(env, input);
}