package de.audi.app.terminalmode.device;

import de.audi.app.terminalmode.device.TMDevice;
import de.audi.app.terminalmode.device.TMDeviceControl;
import de.audi.atip.utils.generics.GIterator;

private class DeviceManager.DeviceActivationRequestHandler
implements TMDeviceControl.IDeviceControlToDeviceManager {
    private static final String LOGCLASS = "DeviceManager.DeviceActivationRequestHandler";
    private TMDeviceControl runningActivationRequest = null;

    private DeviceManager.DeviceActivationRequestHandler() {
    }

    public void requestActivation(TMDeviceControl deviceToBeActivated) {
        if (deviceToBeActivated.getTmDevice().equals(DeviceManager.this.currentActiveDevice) && DeviceManager.this.currentActiveDevice.isActive()) {
            DeviceManager.this.logger.log(1000000, "<!> [%1.requestActivation] NOP because already active - %2.", (Object)LOGCLASS, (Object)DeviceManager.this.currentActiveDevice);
        } else if (TMDevice.ConnectionState.NOT_ATTACHED.is(deviceToBeActivated.getTmDevice().connectionState())) {
            DeviceManager.this.logger.log(1000000, "<!> [%1.requestActivation] NOP because not longer attached - %2.", (Object)LOGCLASS, (Object)DeviceManager.this.currentActiveDevice);
            DeviceManager.this.resetStartupActivation();
        } else {
            this.moveSelectionMarker(deviceToBeActivated.getTmDevice());
            if (TMDevice.INVALID.equals(DeviceManager.this.currentActiveDevice)) {
                DeviceManager.this.logger.log(1000000, "<!> [%1.requestActivation] nothing active, activation confirmed for %2", (Object)LOGCLASS, (Object)deviceToBeActivated.getTmDevice());
                this.confirmActivation(deviceToBeActivated);
            } else {
                DeviceManager.this.logger.log(1000000, "<!> [%1.requestActivation] activation postponed because already active - %2", (Object)LOGCLASS, (Object)DeviceManager.this.currentActiveDevice);
                DeviceManager.this.logger.log(1000000, "<!> [%1.requestActivation] saving request - %2", (Object)LOGCLASS, (Object)deviceToBeActivated.getTmDevice());
                this.runningActivationRequest = deviceToBeActivated;
                if (!deviceToBeActivated.getTmDevice().equals(DeviceManager.this.currentActiveDevice)) {
                    DeviceManager.this.control(DeviceManager.this.currentActiveDevice).deactivationConfirmed();
                } else {
                    DeviceManager.this.logger.log(1000000, "<!> [%1.requestActivation] The Device are equals no need to deactivate-", (Object)LOGCLASS);
                }
            }
        }
    }

    public void requestDeactivation(TMDeviceControl tmDeviceControl) {
        boolean activationPending = tmDeviceControl.equals(this.runningActivationRequest);
        DeviceManager.this.logger.log(1000000, "[%1.requestDeactivation] activation pending for %2: %3", (Object)LOGCLASS, (Object)tmDeviceControl, (Object)new Boolean(activationPending));
        if (!activationPending) {
            tmDeviceControl.deactivationConfirmed();
        }
    }

    public void requestDisconnect(TMDeviceControl tmDeviceControl) {
        tmDeviceControl.deactivationConfirmed();
    }

    private void moveSelectionMarker(TMDevice tmDevice) {
        GIterator iterator = DeviceManager.this.deviceMappings.getDeviceList().iterator();
        while (iterator.hasNext()) {
            TMDevice device = (TMDevice)iterator.next();
            if (device == tmDevice) continue;
            device.setSelected(false);
            device.setUserAcceptState(TMDevice.UserAcceptState.NATIVE_SELECTED);
        }
        tmDevice.setSelected(true);
        DeviceManager.this.notifyDeviceListChanged(DeviceManager.this.deviceListListener, DeviceManager.this.getDeviceList(), "moveSelectionMarker for" + tmDevice);
    }

    private void confirmActivation(TMDeviceControl tmDeviceControl) {
        DeviceManager.this.currentActiveDevice = tmDeviceControl.getTmDevice();
        DeviceManager.this.lastActiveDevice = new TMDevice(DeviceManager.this.currentActiveDevice);
        DeviceManager.this.notifyActiveDeviceStateChanged(DeviceManager.this.activeDeviceStateListener, DeviceManager.this.currentActiveDevice, "confirmActivation");
        tmDeviceControl.activationConfirmed();
    }

    public void notifyAboutChange(TMDevice pDevice, String reason) {
        boolean currentActiveDeviceChanged = pDevice.getUniqueID().equals(DeviceManager.this.currentActiveDevice.getUniqueID());
        boolean activeDeviceWasDeactivated = DeviceManager.this.currentActiveDevice.connectionState().isOneOf(TMDevice.ConnectionState.ATTACHED, TMDevice.ConnectionState.NOT_ATTACHED);
        boolean deviceWasActivated = pDevice.connectionState().is(TMDevice.ConnectionState.ACTIVE);
        if (deviceWasActivated && this.runningActivationRequest != null && pDevice.equals(this.runningActivationRequest.getTmDevice())) {
            this.runningActivationRequest = null;
        }
        if (activeDeviceWasDeactivated && this.runningActivationRequest != null && DeviceManager.this.currentActiveDevice.equals(this.runningActivationRequest.getTmDevice())) {
            this.runningActivationRequest = null;
        }
        if (currentActiveDeviceChanged && activeDeviceWasDeactivated) {
            if (this.runningActivationRequest != null) {
                if (!this.runningActivationRequest.getTmDevice().userAcceptState().is(TMDevice.UserAcceptState.DISCLAIMER_ACCEPTED)) {
                    DeviceManager.this.logger.log(1000000, "<!> [%1.notifyAboutChange] disclaimer declined on pending request - ignore %2", (Object)LOGCLASS, (Object)this.runningActivationRequest.getTmDevice());
                    this.runningActivationRequest = null;
                } else {
                    DeviceManager.this.logger.log(1000000, "<!> [%1.notifyAboutChange] activating pending request %2", (Object)LOGCLASS, (Object)this.runningActivationRequest.getTmDevice());
                    this.confirmActivation(this.runningActivationRequest);
                }
            } else {
                DeviceManager.this.logger.log(1000000, "[%1.notifyAboutChange] no pending requests present", (Object)LOGCLASS);
                DeviceManager.this.currentActiveDevice.setSelected(false);
                DeviceManager.this.currentActiveDevice = TMDevice.INVALID;
                DeviceManager.this.notifyActiveDeviceStateChanged(DeviceManager.this.activeDeviceStateListener, DeviceManager.this.currentActiveDevice, "active device deactivated");
            }
        }
        if (currentActiveDeviceChanged && !activeDeviceWasDeactivated) {
            DeviceManager.this.notifyActiveDeviceStateChanged(DeviceManager.this.activeDeviceStateListener, DeviceManager.this.currentActiveDevice, "active device changed");
        }
        if (!currentActiveDeviceChanged && pDevice.isActive()) {
            DeviceManager.this.logger.log(1000000, "[%1.notifyAboutChange] setting active device as selected", (Object)LOGCLASS);
            DeviceManager.this.currentActiveDevice = pDevice;
            DeviceManager.this.currentActiveDevice.setSelected(pDevice.userAcceptState().is(TMDevice.UserAcceptState.DISCLAIMER_ACCEPTED));
            DeviceManager.this.notifyActiveDeviceStateChanged(DeviceManager.this.activeDeviceStateListener, DeviceManager.this.currentActiveDevice, "active device selected");
        }
        DeviceManager.this.notifyDeviceListChanged(DeviceManager.this.deviceListListener, DeviceManager.this.getDeviceList(), reason + "for device: " + pDevice);
    }

    public void deleteMe(TMDeviceControl tmDeviceControl) {
        DeviceManager.this.deviceMappings.deleteDeviceFromPersistence(tmDeviceControl);
        if (tmDeviceControl.getTmDevice().equals(DeviceManager.this.currentActiveDevice)) {
            DeviceManager.this.currentActiveDevice = TMDevice.INVALID;
        }
        this.notifyAboutChange(TMDevice.INVALID, "deleted");
    }
}
