#ifndef Provision_h
#define Provision_h

// Local includes.
#include "Provision_Callback.h"
#include "IAPI_Implementation.h"


// Provision topics.
char constexpr PROV_RESPONSE_TOPIC[] = "/provision/response";
char constexpr PROV_REQUEST_TOPIC[] = "/provision/request";
// Provision data keys.
char constexpr DEVICE_NAME_KEY[] = "deviceName";
char constexpr PROV_DEVICE_KEY[] = "provisionDeviceKey";
char constexpr PROV_DEVICE_SECRET_KEY[] = "provisionDeviceSecret";
char constexpr PROV_CRED_TYPE_KEY[] = "credentialsType";
char constexpr PROV_TOKEN[] = "token";
char constexpr PROV_CRED_USERNAME[] = "username";
char constexpr PROV_CRED_PASSWORD[] = "password";
char constexpr PROV_CRED_CLIENT_ID[] = "clientId";
char constexpr PROV_CRED_HASH[] = "hash";


/// @brief Handles the internal implementation of the ThingsBoard provision API.
/// See https://thingsboard.io/docs/user-guide/device-provisioning/ for more information
/// @tparam Logger Implementation that should be used to print error messages generated by internal processes and additional debugging messages if THINGSBOARD_ENABLE_DEBUG is set, default = DefaultLogger
template <typename Logger = DefaultLogger>
class Provision : public IAPI_Implementation {
  public:
    /// @brief Constructor
    Provision() = default;

    /// @brief Sends provisioning request for a new device, meaning we want to create a device that we can then connect over,
    /// where the given provision device key / secret decide which device profile is used to create the given device with.
    /// Optionally a device name can be passed or be left empty (cloud will use a random string as the name instead).
    /// The cloud then sends back json data containing our credentials, that will call the given callback, if creating the device was successful.
    /// The data contained in that callbackcan then be used to disconnect and reconnect to the ThingsBoard server as our newly created device.
    /// that will be called if a response from the server for the method with the given name is received.
    /// Because the provision request is a single event subscription, meaning we only ever receive a response to our request once,
    /// we automatically unsubscribe and delete the internal allocated data for the request as soon as the response has been received and handled by the subscribed callback.
    /// See https://thingsboard.io/docs/user-guide/device-provisioning/ for more information
    /// @param callback Callback method that will be called upon data arrival with the given data that was received serialized into a JsonDocument
    /// @return Whether sending the provisioning request was successful or not
    bool Provision_Request(Provision_Callback const & callback) {
        char const * provision_device_key = callback.Get_Device_Key();
        char const * provision_device_secret = callback.Get_Device_Secret();

        if (Helper::stringIsNullorEmpty(provision_device_key) || Helper::stringIsNullorEmpty(provision_device_secret)) {
            return false;
        }
        else if (!Provision_Subscribe(callback)) {
            return false;
        }

        StaticJsonDocument<JSON_OBJECT_SIZE(9)> request_buffer;
        char const * device_name = callback.Get_Device_Name();
        char const * access_token = callback.Get_Device_Access_Token();
        char const * cred_username = callback.Get_Credentials_Username();
        char const * cred_password = callback.Get_Credentials_Password();
        char const * cred_client_id = callback.Get_Credentials_Client_ID();
        char const * hash = callback.Get_Certificate_Hash();
        char const * credentials_type = callback.Get_Credentials_Type();

        // Deciding which underlying provisioning method is restricted, by the Provision_Callback class.
        // Meaning only the key-value pairs that are needed for the given provisioning method are set,
        // resulting in the rest not being sent and therefore the provisioning request having the correct formatting
        if (!Helper::stringIsNullorEmpty(device_name)) {
            request_buffer[DEVICE_NAME_KEY] = device_name;
        }
        if (!Helper::stringIsNullorEmpty(access_token)) {
            request_buffer[PROV_TOKEN] = access_token;
        }
        if (!Helper::stringIsNullorEmpty(cred_username)) {
            request_buffer[PROV_CRED_USERNAME] = cred_username;
        }
        if (!Helper::stringIsNullorEmpty(cred_password)) {
            request_buffer[PROV_CRED_PASSWORD] = cred_password;
        }
        if (!Helper::stringIsNullorEmpty(cred_client_id)) {
            request_buffer[PROV_CRED_CLIENT_ID] = cred_client_id;
        }
        if (!Helper::stringIsNullorEmpty(hash)) {
            request_buffer[PROV_CRED_HASH] = hash;
        }
        if (!Helper::stringIsNullorEmpty(credentials_type)) {
            request_buffer[PROV_CRED_TYPE_KEY] = credentials_type;
        }
        request_buffer[PROV_DEVICE_KEY] = provision_device_key;
        request_buffer[PROV_DEVICE_SECRET_KEY] = provision_device_secret;
        return m_send_json_callback.Call_Callback(PROV_REQUEST_TOPIC, request_buffer, Helper::Measure_Json(request_buffer));
    }

    API_Process_Type Get_Process_Type() const override {
        return API_Process_Type::JSON;
    }

    void Process_Response(char * const topic, uint8_t * payload, unsigned int length) override {
        // Nothing to do
    }

    void Process_Json_Response(char * const topic, JsonDocument const & data) override {
        m_provision_callback.Call_Callback(data);
        // Unsubscribe from the provision response topic,
        // Will be resubscribed if another request is sent anyway
        (void)Provision_Unsubscribe();
    }

    char const * Get_Response_Topic_String() const override {
        return PROV_RESPONSE_TOPIC;
    }

    bool Unsubscribe() override {
        return Provision_Unsubscribe();
    }

    bool Resubscribe_Topic() override {
        return Unsubscribe();
    }

#if !THINGSBOARD_USE_ESP_TIMER
    void loop() override {
        // Nothing to do
    }
#endif // !THINGSBOARD_USE_ESP_TIMER

    void Initialize() override {
        // Nothing to do
    }

    void Set_Client_Callbacks(Callback<void, IAPI_Implementation &>::function subscribe_api_callback, Callback<bool, char const * const, JsonDocument const &, size_t const &>::function send_json_callback, Callback<bool, char const * const, char const * const>::function send_json_string_callback, Callback<bool, char const * const>::function subscribe_topic_callback, Callback<bool, char const * const>::function unsubscribe_topic_callback, Callback<uint16_t>::function get_size_callback, Callback<bool, uint16_t>::function set_buffer_size_callback, Callback<size_t *>::function get_request_id_callback) override {
        m_send_json_callback.Set_Callback(send_json_callback);
        m_subscribe_topic_callback.Set_Callback(subscribe_topic_callback);
        m_unsubscribe_topic_callback.Set_Callback(unsubscribe_topic_callback);
    }

private:
    /// @brief Subscribes one provision callback,
    /// that will be called if a provision response from the server is received
    /// @param callback Callback method that will be called
    /// @return Whether requesting the given callback was successful or not
    bool Provision_Subscribe(Provision_Callback const & callback) {
        if (!m_subscribe_topic_callback.Call_Callback(PROV_RESPONSE_TOPIC)) {
            Logger::printfln(SUBSCRIBE_TOPIC_FAILED, PROV_RESPONSE_TOPIC);
            return false;
        }
        m_provision_callback = callback;
        return true;
    }

    /// @brief Unsubcribes the provision callback
    /// @return Whether unsubcribing the previously subscribed callback
    /// and from the provision response topic, was successful or not
    bool Provision_Unsubscribe() {
        m_provision_callback = Provision_Callback();
        return m_unsubscribe_topic_callback.Call_Callback(PROV_RESPONSE_TOPIC);
    }

    Callback<bool, char const * const, JsonDocument const &, size_t const &> m_send_json_callback = {};         // Send json document callback
    Callback<bool, char const * const>                                       m_subscribe_topic_callback = {};   // Subscribe mqtt topic client callback
    Callback<bool, char const * const>                                       m_unsubscribe_topic_callback = {}; // Unubscribe mqtt topic client callback

    Provision_Callback                                                       m_provision_callback = {};         // Provision response callback
};

#endif // Provision_h
