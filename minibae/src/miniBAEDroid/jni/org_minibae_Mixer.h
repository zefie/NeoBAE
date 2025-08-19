// ...existing includes...
// Added debug JNI declarations
JNIEXPORT jint JNICALL Java_org_minibae_Mixer__1debugGetLevels(JNIEnv*, jclass, jlong, jintArray);
JNIEXPORT void JNICALL Java_org_minibae_Mixer__1debugForceUnityScaling(JNIEnv*, jclass, jboolean);
JNIEXPORT jboolean JNICALL Java_org_minibae_Mixer__1debugIsUnityScalingForced(JNIEnv*, jclass);
