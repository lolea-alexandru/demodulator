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

// Otsu thresholding
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_example_demodulator_OpenCVUtils_otsuThreshold(
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
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(175, 160));
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
            binary, labels, stats, centroids, 8, CV_32S);

    // Return the colors to the dst
    src.copyTo(dst);

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

        cv::rectangle(dst, bbox, cv::Scalar(0, 255, 0, 255), 2);
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

    AndroidBitmap_unlockPixels(env, output);
    AndroidBitmap_unlockPixels(env, input);

    return ledBounds;
}