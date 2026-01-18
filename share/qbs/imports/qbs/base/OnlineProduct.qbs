import qbs.FileInfo
import qbs.File

Product {
    id: root
    property string uri
    property path sourceCache
    property path onlineBuildDirectory: project.buildDirectory
    readonly property path onlineSourceDirectory: probe.sourceDirectory

    type: ["onlinesource-cleanup"]

    OnlineSource {
        id: probe
        name: root.name
        uri: root.uri
        sourceCache: root.sourceCache
        buildDirectory: root.onlineBuildDirectory
    }

    Export {
        Depends { name: "cpp" }
        cpp.includePaths: probe.includePaths
    }

    Rule {
        multiplex: true
        condition: true
        alwaysRun: true
        Artifact {
            filePath: product.onlineBuildDirectory + "/.onlinesource-cleanup"
            fileTags: ["onlinesource-cleanup"]
        }
        prepare: {
            var cmd = new JavaScriptCommand();
            cmd.description = "cleaning up onlinesource cache";
            cmd.silent = true;
            cmd.sourceCode = function() {
                var cacheFile = FileInfo.joinPaths(product.onlineBuildDirectory, "online-source", "resolution-cache.json");
                if (File.exists(cacheFile)) {
                   File.remove(cacheFile);
                }
            }
            return cmd;
        }
    }
}
