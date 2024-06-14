// In a production environment we should use cert validation, fetch cert from target webserver
// $ openssl s_client -showcerts -connect shelly-106-eu.shelly.cloud:443

#if !IGNORE_SSL_CERT
const char *rootCACertificate = "-----BEGIN CERTIFICATE-----\n"
                                "MIIF6TCCA9GgAwIBAgIQBeTcO5Q4qzuFl8umoZhQ4zANBgkqhkiG9w0BAQwFADCB\n"
                                "......\n"
                                "-----END CERTIFICATE-----\n";
#endif