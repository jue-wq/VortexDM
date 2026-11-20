plugins {
    id("com.android.application")
}
android {
    namespace = "com.vortexdm.app"
    compileSdk = 37
    defaultConfig {
        applicationId = "com.vortexdm.app"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild {
            cmake { cppFlags += listOf("-std=c++23", "-Wall", "-Wextra", "-Wpedantic") }
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.31.6" }
    }
}
dependencies {
    implementation("org.mozilla.geckoview:geckoview-nightly:162.0.20261118215316")
}
