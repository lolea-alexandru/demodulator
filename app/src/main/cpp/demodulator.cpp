// Write C++ code here.
//
// Do not forget to dynamically load the C++ library into your application.
//
// For instance,
//
// In MainActivity.java:
//    static {
//       System.loadLibrary("demodulator");
//    }
//
// Or, in MainActivity.kt:
//    companion object {
//      init {
//         System.loadLibrary("demodulator")
//      }
//    }

#include <jni.h>
#include <string>

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
