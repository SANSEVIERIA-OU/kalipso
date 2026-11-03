// SPDX-License-Identifier: AGPL-3.0-or-later
import QtQuick
import org.kde.plasma.workspace.dbus as DBus
Item {
    id:engine
    visible:false
    property string status:i18n("Choose the language you speak.")
    property bool active:false
    property bool failed:false
    property bool busy:false
    property string backend:"moonshine"
    property bool guided:false // The companion's recognizer learnt the script (see Tracker.guided).
    property string language:""
    property bool ready:false  // A language model is installed and the companion can listen.
    // The catalog languages ({code, name, backend, ready, bytes}) and the download in progress
    // ({active, stage, language, received, total, error}); see GetLanguages in the companion.
    property var languages:[]
    property var install:({})
    property bool downloads:true
    property bool languagesUnavailable:false
    property string languagesText:""
    property string languageError:""  // Why the companion refused a language, in its words.
    readonly property bool installing:install.active===true
    property int lastSequence:-1
    property int generation:0
    // The companion's name, or the one it had before Kalipso moved to SANSEVIERIA-OU (a Kalipso 2.0
    // companion still answers only to that): the first call that finds no companion tries the other.
    property string service:"io.github.sansevieria_ou.Kalipso.Engine"
    readonly property string enginePath:"/" + service.replace(/\./g, "/")
    function otherEngine(error) {
        const text = String(error && error.name ? error.name + " " + error.message : error && error.message ? error.message : error)
        if (!/ServiceUnknown|NameHasNoOwner|UnknownObject|UnknownInterface|not provided by any|No such (object|interface)/.test(text))
            return false
        service = service.indexOf("sansevieria_ou") >= 0 ? "io.github.brianidavide.Kalipso.Engine"
                                                           : "io.github.sansevieria_ou.Kalipso.Engine"
        return true
    }
    signal transcript(var event)
    function call(method,callback,epoch,args,retried) {
        const reply=DBus.SessionBus.asyncCall({service:service,path:enginePath,iface:service,member:method,arguments:args||[]})
        const done=function(){
            try {
            if(epoch!==generation)return
            busy=false
            if(reply.isError&&!retried&&otherEngine(reply.error)){call(method,callback,epoch,args,true);return}
            if(reply.isError){failed=true;active=false;poll.stop();console.warn("Local speech: "+reply.error.message);status=i18n("Local speech is unavailable. Open Local speech setup to install the companion and a language model.");return}
            callback(reply.value)
            } finally {reply.destroy()}
        }
        if(reply.isFinished)done();else reply.finished.connect(done)
    }
    function receive(value) {
        const state=JSON.parse(value);active=state.active
        failed=state.status==="error";status=state.message||state.status
        if(state.backend)backend=state.backend
        guided=state.guided===true
        const events=state.events||[state]
        for(const event of events){if(Number.isInteger(event.sequence)&&event.sequence>lastSequence){lastSequence=event.sequence;transcript(event)}}
        if(!active)poll.stop()
    }
    function settings() {
        if(active||busy)return
        busy=true
        call('GetDependencies',function(value){
            const deps=JSON.parse(value);backend=deps.backend;language=deps.language||"";ready=deps.ready
            const engineName=backend==="moonshine"?"Moonshine":"Vosk"
            status=deps.ready?i18n("Ready to listen in %1, with %2.",nameOf(language)||i18n("your language"),engineName)
                :!deps.model?i18n("Choose the language you speak.")
                :deps.missing.join(' ')
        },generation)
    }
    function nameOf(code) {
        for(const item of languages)if(item.code===code)return languageName(item)
        return code
    }
    // A language named in the desktop's language, so a missing font never shows its name as boxes.
    function languageName(language) {
        return ({de: i18n("German"), en: i18n("English"), es: i18n("Spanish"), fr: i18n("French"),
                 it: i18n("Italian"), ja: i18n("Japanese"), zh: i18n("Chinese")})[language.code] || language.name
    }
    function megabytes(bytes) { return Math.round(bytes / 1048576) }
    // What the language download is doing, in words; empty when there is nothing to say.
    readonly property string languageStatus: {
        const job = install
        const name = nameOf(job.language || "")
        if (languagesUnavailable)
            return i18n("This Kalipso companion cannot download languages. Install or update it, then log out and back in.")
        if (languageError !== "")
            return languageError
        if (job.active)
            return job.stage === "unpacking" ? i18n("Checking and unpacking %1…", name)
                : i18n("Downloading %1: %2 of %3 MB", name, megabytes(job.received), megabytes(job.total))
        if (job.stage === "done")
            return i18n("Ready to listen in %1. Choose Follow my voice and start listening.", name)
        if (job.stage === "cancelled")
            return i18n("Download cancelled. Nothing was changed.")
        if (job.stage !== "failed")
            return ""
        switch (job.error) {
        case "network": return i18n("Couldn't download %1. Check the internet connection and try again.", name)
        case "fingerprint": return i18n("The %1 download didn't match its fingerprint, so it was deleted. Try again later.", name)
        case "archive": return i18n("The %1 model couldn't be unpacked. Try again later.", name)
        case "disk": return i18n("Couldn't save %1: there isn't enough free space, or the data folder can't be written.", name)
        case "busy": return i18n("%1 is downloaded. Choose it again when you stop listening.", name)
        default: return i18n("Kalipso downloads speech models on Linux for x86_64 computers.")
        }
    }
    readonly property bool languageRetry: install.stage === "failed"
        && ["network", "fingerprint", "archive", "disk", "busy"].indexOf(install.error) >= 0
    // Language calls never touch the listening state: they have their own replies and errors.
    // Without `failed`, an error means the companion is missing or too old to download languages.
    function request(method,args,callback,failed,retried) {
        const reply=DBus.SessionBus.asyncCall({service:service,path:enginePath,iface:service,member:method,arguments:args})
        const done=function(){
            try {
                if(reply.isError&&!retried&&otherEngine(reply.error)){request(method,args,callback,failed,true);return}
                if(reply.isError){
                    console.warn("Kalipso companion: "+reply.error.message)
                    if(failed){failed(reply.error.message);return}
                    languagesUnavailable=true;languagePoll.stop();return
                }
                languagesUnavailable=false
                callback(reply.value)
            } finally {reply.destroy()}
        }
        if(reply.isFinished)done();else reply.finished.connect(done)
    }
    function refreshLanguages() {
        request('GetLanguages',[],function(value){
            const info=JSON.parse(value)
            const finished=installing&&!info.install.active
            // Replaced only when something changed, so the buttons keep their focus while polling.
            const text=JSON.stringify(info.languages)
            if(text!==languagesText){languagesText=text;languages=info.languages}
            install=info.install;downloads=info.downloads
            language=info.language||"";backend=info.backend||backend
            if(installing)languagePoll.start();else languagePoll.stop()
            if(finished&&!active&&!busy)settings()
        })
    }
    // Listens in this language from now on, downloading its model first if needed.
    function chooseLanguage(code) {
        if(active||busy||installing)return
        languageError=""
        request('InstallLanguage',[code],function(){refreshLanguages();if(!active&&!busy)settings()},
                function(message){languageError=message})
    }
    function cancelInstall() {request('CancelInstall',[],refreshLanguages)}

    // --- The app's scripts (the companion reads them; the app writes them) ---
    // callback({available, current, scripts:[{id, title, modified, words, speed}]}); an older companion
    // or none at all gives {available: false}.
    function listScripts(callback) {
        request('ListScripts',[],function(value){callback(JSON.parse(value))},
                function(){callback({available:false,failed:true,current:"",scripts:[]})})
    }
    // callback({id, title, modified, text, position, speed}), or failed(message) when it is gone.
    function getScript(id,callback,failed) {
        request('GetScript',[id],function(value){callback(JSON.parse(value))},failed||function(){})
    }
    // Opens the app on this script, to change it there.
    function openInApp(id,failed) {request('OpenInApp',[id],function(){},failed||function(){})}

    // The script being read: the companion's Vosk then expects its sentences. Older companions do
    // not know SetScript; they hear any words, as before.
    function setScript(script) {if(active&&script)request('SetScript',[script],function(){},function(){})}
    function start(script) {
        if(active||busy)return
        const epoch=++generation;failed=false;lastSequence=-1;busy=true;status=i18n("Checking local speech components")
        call('GetDependencies',function(value){
            const deps=JSON.parse(value)
            if(!deps.ready){failed=true;status=deps.missing.join(' ');return}
            busy=true
            if(script)request('SetScript',[script],function(){},function(){})
            call('Start',function(started){active=started;busy=true;call('GetState',receive,epoch);if(started)poll.start()},epoch)
        },epoch)
    }
    function stop() {
        ++generation;poll.stop();active=false;busy=false;failed=false
        call('Stop',function(){status=i18n("Microphone stopped")},generation)
    }
    Timer{id:poll;interval:250;repeat:true;onTriggered:{if(!engine.busy){engine.busy=true;engine.call('GetState',engine.receive,engine.generation)}}}
    Timer{id:languagePoll;interval:500;repeat:true;onTriggered:engine.refreshLanguages()}
    Component.onDestruction:{if(active||busy)stop()}
}
