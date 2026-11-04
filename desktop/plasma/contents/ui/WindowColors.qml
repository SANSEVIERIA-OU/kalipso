// SPDX-License-Identifier: LicenseRef-SANSEVIERIA-1.0
import QtQuick
import org.kde.plasma.workspace.dbus as DBus
Item {
    id:colors
    visible:false
    property bool active:false
    property bool busy:false
    property int generation:0
    property string status:i18n("Choose the window behind the reader. Desktop permission is required.")
    property string sample:""
    signal sampled(string color)
    readonly property string clientToken:Date.now().toString(36)+"_"+Math.random().toString(36).slice(2)
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
    function call(method,callback,epoch,retried) {
        const reply=DBus.SessionBus.asyncCall({service:service,path:enginePath,iface:service,member:method,arguments:[clientToken]})
        const done=function(){
            try {
                if(epoch!==generation)return
                busy=false
                if(reply.isError&&!retried&&otherEngine(reply.error)){call(method,callback,epoch,true);return}
                if(reply.isError){active=false;poll.stop();status=i18n("Window colors unavailable. Install or update the Kalipso companion and the desktop sharing portal.");return}
                callback(reply.value)
            } finally {reply.destroy()}
        }
        if(reply.isFinished)done();else reply.finished.connect(done)
    }
    function start(){
        if(active||busy)return
        const epoch=++generation;busy=true;sample="";active=true
        status=i18n("Choose the window behind your reader in the desktop sharing dialog")
        call("StartColors",function(started){active=started;if(started)poll.start()},epoch)
    }
    function stop(){
        const wasActive=active||busy;++generation;active=false;busy=false;poll.stop();sample=""
        status=i18n("Window sharing stopped · Choose a background window to resume")
        if(wasActive)call("StopColors",function(){},generation)
    }
    Timer {
        id:poll;interval:2000;repeat:true;triggeredOnStart:true
        onTriggered:{
            if(colors.busy)return
            colors.busy=true
            colors.call("GetColorState",function(value){
                const state=JSON.parse(value);colors.active=state.active;colors.status=state.message
                if(state.active&&/^#[0-9a-f]{6}$/i.test(state.color)){colors.sample=state.color;colors.sampled(state.color)}
                if(!state.active){poll.stop();colors.call("StopColors",function(){},colors.generation)}
            },colors.generation)
        }
    }
    Component.onDestruction:stop()
}
