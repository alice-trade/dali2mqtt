import { Controller, Get, Post, Param, Body, Put, HttpException, HttpStatus } from '@nestjs/common';
import { DaliService } from './dali.service';
import { SetBrightnessDto } from './dto/set-brightness.dto';

@Controller('dali')
export class DaliController {
    constructor(private readonly daliService: DaliService) {}

    @Get('devices')
    getDevices() {
        return this.daliService.getAllDevices();
    }

    @Get('devices/:addr')
    async getDevice(@Param('addr') addr: string) {
        return this.daliService.getDeviceState(addr);
    }

    @Post('devices/:addr/on')
    async turnOn(@Param('addr') addr: string) {
        return this.daliService.turnOn(addr);
    }

    @Post('devices/:addr/off')
    async turnOff(@Param('addr') addr: string) {
        return this.daliService.turnOff(addr);
    }

    @Put('devices/:addr/brightness')
    async setBrightness(
        @Param('addr') addr: string,
        @Body() dto: SetBrightnessDto
    ) {
        return this.daliService.setBrightness(addr, dto.level);
    }
}