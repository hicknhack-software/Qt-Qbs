import qbs.FileInfo
import qbs.File
import "onlinesource.js" as OnlineSourceHelper

Probe {
    id: probe
    property string name
    property string uri
    property string projectFile
    property path sourceCache
    property path buildDirectory: project.buildDirectory

    // Results
    property path sourceDirectory
    property path projectFilePath
    property pathList includePaths

    configure: {
        var baseDir = probe.buildDirectory;
        var outputBaseDir = FileInfo.joinPaths(baseDir, "online-source");
        var packages = {};
        packages[probe.name] = { uri: probe.uri, projectFile: probe.projectFile };

        var result = OnlineSourceHelper.configure(probe.name, outputBaseDir, probe.sourceCache, packages);
        if (result) {
            sourceDirectory = result.sourceDirectory;
            if (result.projectFile) projectFilePath = result.projectFile;
            includePaths = result.includePaths;
            found = true;
        } else {
            found = false;
        }
    }
}
