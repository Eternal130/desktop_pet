package com.desktoppet.core;

import com.desktoppet.model.PetState;

import java.util.concurrent.locks.ReentrantReadWriteLock;

public class PetStateManager {

    private String currentModelName = "";
    private int windowX = 0;
    private int windowY = 0;
    private boolean connected = false;
    private boolean modelLoaded = false;

    private final ReentrantReadWriteLock lock = new ReentrantReadWriteLock();

    public PetState getState() {
        lock.readLock().lock();
        try {
            return new PetState(currentModelName, windowX, windowY, connected, modelLoaded);
        } finally {
            lock.readLock().unlock();
        }
    }

    public void updateModelName(String name) {
        lock.writeLock().lock();
        try {
            this.currentModelName = name;
        } finally {
            lock.writeLock().unlock();
        }
    }

    public void updateWindowPosition(int x, int y) {
        lock.writeLock().lock();
        try {
            this.windowX = x;
            this.windowY = y;
        } finally {
            lock.writeLock().unlock();
        }
    }

    public void setConnected(boolean connected) {
        lock.writeLock().lock();
        try {
            this.connected = connected;
        } finally {
            lock.writeLock().unlock();
        }
    }

    public void setModelLoaded(boolean loaded) {
        lock.writeLock().lock();
        try {
            this.modelLoaded = loaded;
        } finally {
            lock.writeLock().unlock();
        }
    }
}
