//
// Created by vastrakai on 10/16/2024.
//

#include "VoiceChat.hpp"
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.h>
#include <spdlog/spdlog.h>
#include <thread>

using namespace winrt;
using namespace Windows::Networking;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;

class VoiceClient {
public:
    VoiceClient(const std::string& serverIP, int serverPort)
        : serverIP(serverIP), serverPort(serverPort) {
        winrt::init_apartment();  // Initialize the WinRT apartment
    }

    void connectToServer() {
        try {
            // Create the StreamSocket and connect it to the server
            socket.ConnectAsync(HostName(winrt::to_hstring(serverIP)), L"1845").get();
            spdlog::info("Successfully connected to the server!");

            // Set up DataWriter for sending data
            writer = DataWriter(socket.OutputStream());
        } catch (hresult_error const& ex) {
            spdlog::error("Failed to connect to server: {}", winrt::to_string(ex.message()));
        }
    }

    IAsyncAction sendAudioDataAsync(const std::vector<BYTE>& audioBuffer) {
        try {
            // Write audio data to the output stream
            writer.WriteBytes(array_view<const BYTE>(audioBuffer));
            co_await writer.StoreAsync(); // Store and send the data asynchronously
        } catch (hresult_error const& ex) {
            spdlog::error("Failed to send audio data: {}", winrt::to_string(ex.message()));
        }
    }

    void closeConnection() {
        writer.Close(); // Close the DataWriter
        socket.Close(); // Close the socket
    }

    std::string serverIP;
    int serverPort;
    StreamSocket socket;
    DataWriter writer{ nullptr };
};


void VoiceChat::onEnable()
{

}

void VoiceChat::onDisable()
{

}

std::unique_ptr<VoiceClient> client = nullptr;


void VoiceChat::onInit()
{
    /*// Create a client to connect to the server
    client = std::make_unique<VoiceClient>("127.0.0.1", 1845);

    // Start the async connection in a new thread
    std::thread([&]() {
        client->connectToServer();  // Connect to the server
        // Wait for the connection to be established
        std::this_thread::sleep_for(std::chrono::seconds(1));


        // After connecting, start recording audio and sending it to the server
        AudioUtils::recordVoiceClip([&](const std::vector<BYTE>& audioBuffer) {
            client->sendAudioDataAsync(audioBuffer).get(); // Send audio data asynchronously
        });
    }).detach();  // Detach the thread to allow continuous recording and sending*/
}
