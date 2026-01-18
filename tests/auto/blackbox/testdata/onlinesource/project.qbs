import qbs.FileInfo

// Test project for onlinesource module provider
Project {
    id: root

    // Test 1: CMake-style package via OnlineProduct
    OnlineProduct {
        name: "testmodule"
        uri: "file://" + project.sourceDirectory + "/local-package"
        property bool dummy: {
            console.info("testmodule.sourceDirectory: " + onlineSourceDirectory);
        }
    }

    Product {
        name: "onlinesource-test"
        Depends { name: "testmodule" }
        property bool dummy: {
            console.info("testmodule.cpp.includePaths: " + testmodule.cpp.includePaths);
        }
    }

    // Test 2: Qbs project package via OnlineSubProject
    OnlineSubProject {
        name: "qbspackage"
        uri: "file://" + root.sourceDirectory + "/qbs-package"
    }
}
