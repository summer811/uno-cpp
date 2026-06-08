#ifndef MUSIC_H
#define MUSIC_H

#include <string>
#include <conio.h>
#include <windows.h>
#include <graphics.h>
#pragma comment(lib,"winmm.lib")
using namespace std;

class music
{
private:
    string musicPath;
    string alias;
    int volume;
    bool repeat;
    static string currentAlias;
    static music* currentMusic;

public:
    music(string musicPath_t, int volume_t, string alias_t, bool repeat_t = true)
    {
        musicPath = musicPath_t;
        volume = volume_t;
        alias = alias_t;
        repeat = repeat_t;
    }

    void playMusic()
    {
        if (!currentAlias.empty()) {
            string stopCmd = "stop " + currentAlias;
            mciSendStringA(stopCmd.c_str(), NULL, 0, NULL);
            string closeCmd = "close " + currentAlias;
            mciSendStringA(closeCmd.c_str(), NULL, 0, NULL);
        }

        string cmd = "open \"" + musicPath + "\" alias " + alias;
        mciSendStringA(cmd.c_str(), NULL, 0, NULL);

        string playCmd = "play " + alias;
        if (repeat) playCmd += " repeat";
        mciSendStringA(playCmd.c_str(), NULL, 0, NULL);

        setVolume(volume);

        currentAlias = alias;
        currentMusic = this;
    }

    void setVolume(int volume)
    {
        this->volume = volume;
        string cmd = "setaudio " + alias + " volume to " + to_string(volume);
        mciSendStringA(cmd.c_str(), NULL, 0, NULL);
    }

    int getVolume() { return volume; }
    string getAlias() { return alias; }

    static string getCurrentAlias() { return currentAlias; }

    static void adjustCurrentVolume(int newVolume)
    {
        if (!currentAlias.empty()) {
            string cmd = "setaudio " + currentAlias + " volume to " + to_string(newVolume);
            mciSendStringA(cmd.c_str(), NULL, 0, NULL);
            if (currentMusic != nullptr) {
                currentMusic->volume = newVolume;
            }
        }
    }

    static int getCurrentVolume()
    {
        if (currentMusic != nullptr) {
            return currentMusic->volume;
        }
        return -1;
    }
};

string music::currentAlias = "";
music* music::currentMusic = nullptr;

music welcome("Assets/Music/welcome.mp3", 500, "welcome");
music normal("Assets/Music/normal.mp3", 500, "normal");
music normal2("Assets/Music/normal2.mp3", 500, "normal2");
music exciting("Assets/Music/exciting.mp3", 500, "exciting");
music win("Assets/Music/win.mp3", 500, "win", false);
music lose("Assets/Music/lose.mp3", 500, "lose", false);

#endif
