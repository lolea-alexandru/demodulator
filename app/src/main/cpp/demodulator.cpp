#include <jni.h>
#include <string>
#include <opencv2/opencv.hpp>
#include <android/bitmap.h>
#include <android/log.h>

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
    cv::Mat imageMat(info.height, info.width, CV_8UC4, pixels);

    // --- You now have a working OpenCV Matrix! ---

    // 4. Read properties directly from the OpenCV Matrix
    int matWidth = imageMat.cols;
    int matHeight = imageMat.rows;
    int matChannels = imageMat.channels();

    // Example: Read the center pixel's color values
    cv::Vec4b centerPixel = imageMat.at<cv::Vec4b>(matHeight / 2, matWidth / 2);
    int blue = centerPixel[0]; // OpenCV loads RGBA as BGRA natively
    int green = centerPixel[1];
    int red = centerPixel[2];

    // 5. Unlock the pixels to hand control back to Android
    AndroidBitmap_unlockPixels(env, bitmap);

    // 6. Format a summary string to return to Kotlin
    char result[256];
    snprintf(result, sizeof(result),
             "OpenCV Mat Properties:\nWidth: %d\nHeight: %d\nChannels: %d\nCenter RGB: (%d, %d, %d)",
             matWidth, matHeight, matChannels, red, green, blue);

    return env->NewStringUTF(result);
}