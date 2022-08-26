#include <chrono>
#include <iostream>
#include <functional>
#include <map>
#include <memory>
#include <microhttpd.h>
#include <string>
#include <thread>

struct RequestContext
{
    MHD_Connection * connection;
    const char * url;
    const char * method;
    const char * upload_data;
    size_t * upload_data_size;
};

using Handler =
    std::function<MHD_Result(const std::shared_ptr< RequestContext >)>;

namespace
{
MHD_Result routingCallback(
    void *, MHD_Connection *,
    const char *, const char *, const char *, const char *,
    size_t *, void **);
std::string identifyRequest(std::string, std::string);
}

class ErrorHandler
{
public:
    MHD_Result notFound(const std::shared_ptr< RequestContext > _ctx)
    {
        std::cout << "not found check called" << std::endl;

        std::string body = "Unknown request: \""
                           + std::string(_ctx->url)
                           + "\"\n";
        struct MHD_Response * response = MHD_create_response_from_buffer(
                                             body.length(),
                                             (void *)body.c_str(),
                                             MHD_RESPMEM_MUST_COPY);

        MHD_Result ret = MHD_queue_response(_ctx->connection, MHD_HTTP_NOT_FOUND,
                                            response);
        MHD_destroy_response(response);

        return ret;
    }
};

class HealthHandler
{
public:
    MHD_Result handle(const std::shared_ptr< RequestContext > _ctx)
    {
        std::cout << "health check called" << std::endl;

        std::string body = "{\"status\":\"UP\"}\n";
        struct MHD_Response * response = MHD_create_response_from_buffer(
                                             body.length(),
                                             (void *)body.c_str(),
                                             MHD_RESPMEM_MUST_COPY);

        MHD_Result ret = MHD_queue_response(_ctx->connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        return ret;
    }
};

class Server
{
public:
    Server()
    {
        m_service = NULL;
    }

    ~Server()
    {
        if(m_service)
        {
            stop();
        }
    }

    void notFound(const Handler & _handler)
    {
        m_not_found_handler = _handler;
    }

    void route(
        const std::string & _path,
        const std::string & _method,
        const Handler & _handler)
    {
        auto key = identifyRequest(_path, _method);
        m_handlers.emplace(key, _handler);
    }

    void start(
        const std::string &,
        unsigned int _port,
        unsigned int)
    {
        std::cout << "Service fired up at " << _port << std::endl;

        m_service = MHD_start_daemon(
                        MHD_USE_THREAD_PER_CONNECTION,
                        _port, NULL, NULL,
                        &routingCallback, this, MHD_OPTION_END);

        if(! m_service)
        {
            //TODO [CMP] handle error
        }
    }

    void stop()
    {
        MHD_stop_daemon(m_service);
        m_service = NULL;
    }

    MHD_Result routing(
        const RequestContext _ctx,
        const char * _version,
        void **)
    {
        std::cout << "Request: " << _ctx.url
                  << ", Method: " << _ctx.method
                  << ", Version: " << _version
                  << std::endl;

        auto key = identifyRequest(_ctx.url, _ctx.method);
        auto handler_pair = m_handlers.find(key);

        if(handler_pair != m_handlers.end())
        {
            const auto & handler = handler_pair->second;

            return handler(std::make_shared<RequestContext>(_ctx));
        }

        if(m_not_found_handler != NULL)
        {
            return m_not_found_handler(std::make_shared<RequestContext>(_ctx));
        }

        return noHandler(std::make_shared<RequestContext>(_ctx));
    }

    MHD_Result noHandler(const std::shared_ptr< RequestContext > _ctx)
    {
        std::string body = "not found";

        struct MHD_Response * response = MHD_create_response_from_buffer(
                                             body.length(),
                                             (void *)body.c_str(),
                                             MHD_RESPMEM_MUST_COPY);

        MHD_Result ret = MHD_queue_response(_ctx->connection, MHD_HTTP_NOT_FOUND,
                                            response);
        MHD_destroy_response(response);

        return ret;
    }


private:
    MHD_Daemon * m_service;
    std::map<std::string, const Handler> m_handlers;
    Handler m_not_found_handler;
};

namespace
{
MHD_Result routingCallback(
    void * _cls,
    MHD_Connection * _connection,
    const char * _url,
    const char * _method,
    const char * _version,
    const char * _upload_data,
    size_t * _upload_data_size,
    void ** _ptr)
{
    Server * server = (Server *) _cls;

    return server->routing(
    {
        _connection,
        _url,
        _method,
        _upload_data,
        _upload_data_size
    },
    _version, _ptr);
}

std::string identifyRequest(std::string _path, std::string _method)
{
    auto method = _method;
    for(auto & c : method)
    {
        c = std::toupper(c);
    }

    return _path + ':' + method;
}
}

int main(const int /*_argc*/, const char ** /*_argv*/)
{
    ErrorHandler eh;
    HealthHandler hh;

    Server server;
    server.notFound(
        std::bind(&ErrorHandler::notFound, eh, std::placeholders::_1));
    server.route(
        "/health", "GET",
        std::bind(&HealthHandler::handle, hh, std::placeholders::_1));
    server.start("127.0.0.1", 8080, 2);

    // TODO [cmp] handle stop events
    std::this_thread::sleep_for(std::chrono::milliseconds(1000 * 60 * 60 * 24 * 7));

    return EXIT_SUCCESS;
}
