'use strict'

const {ipcRenderer} = require('electron')

const {guestInstanceId, openerId} = process

require('./window-setup')(ipcRenderer, guestInstanceId, openerId)
